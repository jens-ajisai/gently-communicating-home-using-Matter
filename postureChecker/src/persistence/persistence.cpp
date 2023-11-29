#include "persistence.h"

#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

LOG_MODULE_REGISTER(persistence, CONFIG_MAIN_LOG_LEVEL);

/* Matches LFS_NAME_MAX */
#define MAX_PATH_LEN 255

int Persistence::lsdir(const char *path) {
  int res;
  struct fs_dir_t dirp;
  static struct fs_dirent entry;

  fs_dir_t_init(&dirp);

  /* Verify fs_opendir() */
  res = fs_opendir(&dirp, path);
  if (res) {
    LOG_ERR("Error opening dir %s [%d]\n", path, res);
    return res;
  }

  LOG_INF("\nListing dir %s ...\n", path);
  for (;;) {
    /* Verify fs_readdir() */
    res = fs_readdir(&dirp, &entry);

    /* entry.name[0] == 0 means end-of-dir */
    if (res || entry.name[0] == 0) {
      if (res < 0) {
        LOG_ERR("Error reading dir [%d]\n", res);
      }
      break;
    }

    if (entry.type == FS_DIR_ENTRY_DIR) {
      LOG_INF("[DIR ] %s\n", entry.name);
    } else {
      LOG_INF("[FILE] %s (size = %zu)\n", entry.name, entry.size);
    }
  }

  /* Verify fs_closedir() */
  fs_closedir(&dirp);

  return res;
}

int Persistence::readFile(const char *path, void *buf, uint16_t len, ReadCallback cb, void *ctx) {
  if (!mInitialized) return -1;

  int err;
  struct fs_dirent dirent;
  struct fs_file_t file;
  ssize_t read_total = 0;

  char fname[MAX_PATH_LEN];
  snprintf(fname, sizeof(fname), "%s/%s", mp->mnt_point, path);

  err = fs_stat(fname, &dirent);
  if (err != 0) {
    LOG_ERR("File status failed: %d", err);
    return err;
  }

  if (dirent.type != FS_DIR_ENTRY_FILE) {
    LOG_ERR("Provided path is not a file");
    return 0;
  }

  fs_file_t_init(&file);
  err = fs_open(&file, fname, FS_O_READ);
  if (err) {
    LOG_ERR("Failed to open %s (%d)", path, err);
    return -ENOEXEC;
  }

  if (dirent.size <= len) {
    read_total = fs_read(&file, buf, len);
    LOG_INF("file fits into buffer %d bytes from file %s", dirent.size, fname);
    if (cb) cb(buf, read_total, ctx);
    fs_close(&file);
    return read_total;
  }

  while (true) {
    ssize_t read = fs_read(&file, buf, len);
    LOG_INF("read %d bytes from file %s", read, fname);
    if (read <= 0) {
      break;
    }
    read_total += read;
    if (cb) cb(buf, read, ctx);
  }

  fs_close(&file);
  return read_total;
}

int Persistence::writeFile(const char *path, void *data, uint16_t len, uint16_t offset,
                           bool append) {
  if (!mInitialized) return -1;

  char fname[MAX_PATH_LEN];
  snprintf(fname, sizeof(fname), "%s/%s", mp->mnt_point, path);
  int rc, ret;

  struct fs_file_t file;
  fs_file_t_init(&file);

  rc = fs_open(&file, fname, FS_O_CREATE | FS_O_APPEND | FS_O_WRITE);
  if (rc < 0) {
    LOG_ERR("FAIL: open %s: %d", fname, rc);
    return rc;
  }

  rc = fs_seek(&file, append ? 0 : offset, append ? FS_SEEK_END : FS_SEEK_SET);
  if (rc < 0) {
    LOG_ERR("FAIL: seek %s: %d", fname, rc);
    goto out;
  }

  rc = fs_write(&file, data, len);
  if (rc < 0) {
    LOG_ERR("FAIL: write %s: %d", fname, rc);
    goto out;
  }

out:
  ret = fs_close(&file);
  if (ret < 0) {
    LOG_ERR("FAIL: close %s: %d", fname, ret);
    return ret;
  }

  return (rc < 0 ? rc : 0);
}

int Persistence::deleteFile(const char *path) {
  if (!mInitialized) return -1;

  int rc;
  char fname[MAX_PATH_LEN];
  snprintf(fname, sizeof(fname), "%s/%s", mp->mnt_point, path);
  rc = fs_unlink(fname);

  if (rc < 0) {
    LOG_ERR("FAIL: unlink %s: [rd:%d]", fname, rc);
    return rc;
  }
  LOG_INF("Unlinked %s", fname);

  return 0;
}

size_t Persistence::getFileSize(const char *path) {
  if (!mInitialized) return -1;

  int err;
  struct fs_dirent dirent;

  char fname[MAX_PATH_LEN];
  snprintf(fname, sizeof(fname), "%s/%s", mp->mnt_point, path);

  err = fs_stat(fname, &dirent);
  if (err != 0) {
    LOG_ERR("File status failed: %d", err);
    return err;
  }

  if (dirent.type != FS_DIR_ENTRY_FILE) {
    LOG_ERR("Provided path is not a file");
    return 0;
  }

  return dirent.size;
}

int Persistence::increaseBootCounter() {
  char fname[MAX_PATH_LEN];
  snprintf(fname, sizeof(fname), "%s/boot_count", mp->mnt_point);

  uint8_t boot_count = 0;
  struct fs_file_t file;
  int rc, ret;

  fs_file_t_init(&file);
  rc = fs_open(&file, fname, FS_O_CREATE | FS_O_RDWR);
  if (rc < 0) {
    LOG_ERR("FAIL: open %s: %d", fname, rc);
    return rc;
  }

  rc = fs_read(&file, &boot_count, sizeof(boot_count));
  if (rc < 0) {
    LOG_ERR("FAIL: read %s: [rd:%d]", fname, rc);
    goto out;
  }
  LOG_INF("%s read count:%u (bytes: %d)\n", fname, boot_count, rc);

  rc = fs_seek(&file, 0, FS_SEEK_SET);
  if (rc < 0) {
    LOG_ERR("FAIL: seek %s: %d", fname, rc);
    goto out;
  }

  boot_count += 1;
  rc = fs_write(&file, &boot_count, sizeof(boot_count));
  if (rc < 0) {
    LOG_ERR("FAIL: write %s: %d", fname, rc);
    goto out;
  }

  LOG_INF("%s write new boot count %u: [wr:%d]\n", fname, boot_count, rc);

out:
  ret = fs_close(&file);
  if (ret < 0) {
    LOG_ERR("FAIL: close %s: %d", fname, ret);
    return ret;
  }

  return (rc < 0 ? rc : 0);
}

int Persistence::flash_info_and_erase(unsigned int id, bool eraseFlash) {
  const struct flash_area *pfa;
  int rc;

  rc = flash_area_open(id, &pfa);
  if (rc < 0) {
    LOG_ERR("FAIL: unable to find flash area %u: %d\n", id, rc);
    return rc;
  }

  LOG_INF("Area %u at 0x%x on %s for %u bytes\n", id, (unsigned int)pfa->fa_off, pfa->fa_dev->name,
          (unsigned int)pfa->fa_size);

  /* Optional wipe flash contents */
  if (eraseFlash) {
    rc = flash_area_erase(pfa, 0, pfa->fa_size);
    LOG_INF("Erasing flash area ... %d", rc);
  }

  flash_area_close(pfa);
  return rc;
}

int Persistence::init(bool eraseFlash) {
  struct fs_statvfs sbuf;
  int rc;

  if (mInitialized) return -1;

  rc = flash_info_and_erase((uintptr_t)mp->storage_dev, eraseFlash);
  if (rc < 0) {
    return rc;
  }

  rc = fs_mount(mp);
  if (rc < 0) {
    LOG_ERR("Failed to mount filesystem");
    goto out;
  }
  LOG_INF("%s mounted\n", mp->mnt_point);

  rc = fs_statvfs(mp->mnt_point, &sbuf);
  if (rc < 0) {
    LOG_ERR("FAIL: statvfs: %d\n", rc);
    goto out;
  }

  LOG_INF(
      "%s: bsize = %lu ; frsize = %lu ;"
      " blocks = %lu ; bfree = %lu\n",
      mp->mnt_point, sbuf.f_bsize, sbuf.f_frsize, sbuf.f_blocks, sbuf.f_bfree);

  rc = lsdir(mp->mnt_point);
  if (rc < 0) {
    LOG_ERR("FAIL: lsdir %s: %d\n", mp->mnt_point, rc);
    goto out;
  }

  increaseBootCounter();
  mInitialized = true;
  return 0;

out:
  rc = fs_unmount(mp);
  LOG_ERR("%s unmount: %d\n", mp->mnt_point, rc);
  return -1;
}

int Persistence::deinit(void) {
  if (!mInitialized) return -1;

  int rc = fs_unmount(mp);
  LOG_INF("%s unmount: %d\n", mp->mnt_point, rc);
  return 0;
}