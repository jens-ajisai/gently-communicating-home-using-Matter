#include "persistence.h"

#include <ff.h>
#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

LOG_MODULE_REGISTER(p, CONFIG_MAIN_LOG_LEVEL);

static FATFS fat_fs;

static struct fs_mount_t mp = {
    .type = FS_FATFS,
    .fs_data = &fat_fs,
    // The exact format is important for fatfs!
    .mnt_point = "/NAND:",
};

bool mInitialized = false;

#define MAX_PATH_LEN 255

int fs_lsdir(const char *path) {
  int res;
  struct fs_dir_t dirp;
  static struct fs_dirent entry;

  fs_dir_t_init(&dirp);

  res = fs_opendir(&dirp, path);
  if (res) {
    LOG_ERR("Error opening dir %s [%d]", path, res);
    return res;
  }

  LOG_INF("Listing dir %s ...", path);
  for (;;) {
    res = fs_readdir(&dirp, &entry);

    /* entry.name[0] == 0 means end-of-dir */
    if (res || entry.name[0] == 0) {
      if (res < 0) {
        LOG_ERR("Error reading dir [%d]", res);
      }
      break;
    }

    if (entry.type == FS_DIR_ENTRY_DIR) {
      LOG_INF("[DIR ] %s", entry.name);
    } else {
      LOG_INF("[FILE] %s (size = %zu)", entry.name, entry.size);
    }
  }

  fs_closedir(&dirp);
  return res;
}

int fs_readFile(const char *path, void *buf, uint16_t len, fs_read_cb_t cb, void *ctx) {
  if (!mInitialized) return -1;

  int err;
  struct fs_dirent dirent;
  struct fs_file_t file;
  ssize_t read_total = 0;

  char fname[MAX_PATH_LEN];
  snprintf(fname, sizeof(fname), "%s/%s", mp.mnt_point, path);

  err = fs_stat(fname, &dirent);
  if (err != 0) {
    LOG_ERR("File status of file %s failed: %d", fname, err);
    return -ENOENT;
  }

  if (dirent.type != FS_DIR_ENTRY_FILE) {
    LOG_ERR("Provided path is not a file");
    return -EISDIR;
  }

  fs_file_t_init(&file);
  err = fs_open(&file, fname, FS_O_READ);
  if (err) {
    LOG_ERR("Failed to open %s (%d)", path, err);
    return err;
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

size_t fs_getFileSize(const char *path) {
  if (!mInitialized) return -1;

  int err;
  struct fs_dirent dirent;

  char fname[MAX_PATH_LEN];
  snprintf(fname, sizeof(fname), "%s/%s", mp.mnt_point, path);

  err = fs_stat(fname, &dirent);
  if (err != 0) {
    LOG_ERR("File status of %s failed: %d", fname, err);
    return err;
  }

  if (dirent.type != FS_DIR_ENTRY_FILE) {
    LOG_ERR("Provided path is not a file");
    return 0;
  }

  return dirent.size;
}

int fs_overwriteData(const char *path, void *data, uint16_t len, uint16_t offset) {
  if (!mInitialized) return -1;

  char fname[MAX_PATH_LEN];
  snprintf(fname, sizeof(fname), "%s/%s", mp.mnt_point, path);
  int rc, ret;

  struct fs_file_t file;
  fs_file_t_init(&file);

  rc = fs_open(&file, fname, FS_O_CREATE | FS_O_WRITE);
  if (rc < 0) {
    LOG_ERR("FAIL: open %s: %d", fname, rc);
    return rc;
  }

  rc = fs_seek(&file, offset, FS_SEEK_SET);
  if (rc < 0) {
    LOG_ERR("FAIL: seek %s: %d", fname, rc);
    goto out;
  }

  rc = fs_write(&file, data, len);
  if (rc < 0) {
    LOG_ERR("FAIL: write %s: %d", fname, rc);
    goto out;
  } else {
    LOG_INF("wrote %d bytes to %s", len, fname);
  }

out:
  ret = fs_close(&file);
  if (ret < 0) {
    LOG_ERR("FAIL: close %s: %d", fname, ret);
    return ret;
  }

  return (rc < 0 ? rc : 0);
}

int fs_appendData(const char *path, void *data, uint16_t len) {
  if (!mInitialized) return -1;

  char fname[MAX_PATH_LEN];
  snprintf(fname, sizeof(fname), "%s/%s", mp.mnt_point, path);
  int rc, ret;

  struct fs_file_t file;
  fs_file_t_init(&file);

  rc = fs_open(&file, fname, FS_O_CREATE | FS_O_APPEND | FS_O_WRITE);
  if (rc < 0) {
    LOG_ERR("FAIL: open %s: %d", fname, rc);
    return rc;
  }

  rc = fs_write(&file, data, len);
  if (rc < 0) {
    LOG_ERR("FAIL: write %s: %d", fname, rc);
    goto out;
  } else {
    LOG_INF("wrote %d bytes to %s", len, fname);
  }

out:
  ret = fs_close(&file);
  if (ret < 0) {
    LOG_ERR("FAIL: close %s: %d", fname, ret);
    return ret;
  }

  return (rc < 0 ? rc : 0);
}

int fs_deleteFile(const char *path) {
  if (!mInitialized) return -1;

  int rc;
  char fname[MAX_PATH_LEN];
  snprintf(fname, sizeof(fname), "%s/%s", mp.mnt_point, path);
  rc = fs_unlink(fname);

  if (rc < 0) {
    LOG_ERR("FAIL: unlink %s: [rd:%d]", fname, rc);
    return rc;
  }
  LOG_INF("Unlinked %s", fname);

  return 0;
}

static int increaseBootCounter() {
  char fname[MAX_PATH_LEN];
  snprintf(fname, sizeof(fname), "%s/boot_count", mp.mnt_point);

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
  LOG_INF("%s read count:%u (bytes: %d)", fname, boot_count, rc);

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

  LOG_INF("%s write new boot count %u: [wr:%d]", fname, boot_count, rc);

out:
  ret = fs_close(&file);
  if (ret < 0) {
    LOG_ERR("FAIL: close %s: %d", fname, ret);
    return ret;
  }

  return (rc < 0 ? rc : 0);
}

static int flash_info_and_erase(unsigned int id, bool eraseFlash) {
  const struct flash_area *pfa;
  int rc;

  rc = flash_area_open(id, &pfa);
  if (rc < 0) {
    LOG_ERR("FAIL: unable to find flash area %u: %d", id, rc);
    return rc;
  }

  LOG_INF("Area %u at 0x%x on %s for %u bytes", id, (unsigned int)pfa->fa_off, pfa->fa_dev->name,
          (unsigned int)pfa->fa_size);

  /* Optional wipe flash contents */
  if (eraseFlash) {
    LOG_INF("Erasing flash area ... %d", rc);
    rc = flash_area_erase(pfa, 0, pfa->fa_size);
    LOG_INF("... Done %d", rc);
  }

  flash_area_close(pfa);
  return rc;
}

int fs_init(bool eraseFlash) {
  struct fs_statvfs sbuf;
  int rc;

  if (mInitialized) return -1;

  rc = flash_info_and_erase((uintptr_t) FIXED_PARTITION_ID(ffs1), eraseFlash);
  if (rc < 0) {
    LOG_ERR("Failed to get flash info.");
    return rc;
  }

  rc = fs_mount(&mp);
  if (rc < 0) {
    LOG_ERR("Failed to mount filesystem");
    goto out;
  }

  k_sleep(K_MSEC(50));
  LOG_INF("%s mounted", mp.mnt_point);

  rc = fs_statvfs(mp.mnt_point, &sbuf);
  if (rc < 0) {
    LOG_ERR("FAIL: statvfs: %d", rc);
    goto out;
  }

  LOG_INF(
      "%s: bsize = %lu ; frsize = %lu ;"
      " blocks = %lu ; bfree = %lu",
      mp.mnt_point, sbuf.f_bsize, sbuf.f_frsize, sbuf.f_blocks, sbuf.f_bfree);

  rc = fs_lsdir(mp.mnt_point);
  if (rc < 0) {
    LOG_ERR("FAIL: lsdir %s: %d", mp.mnt_point, rc);
    goto out;
  }

  increaseBootCounter();
  mInitialized = true;
  return 0;

out:
  rc = fs_unmount(&mp);
  LOG_ERR("%s unmount: %d", mp.mnt_point, rc);
  return -1;
}

int fs_deinit(void) {
  if (!mInitialized) return -1;

  int rc = fs_unmount(&mp);
  LOG_INF("%s unmount: %d", mp.mnt_point, rc);
  return 0;
}