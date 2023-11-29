#pragma once

#include <zephyr/device.h>
#include <zephyr/fs/fs.h>

#include "../sensors/sensorDataDefinition.h"

#define PARTITION_NODE DT_NODELABEL(lfs1)

#if DT_NODE_EXISTS(PARTITION_NODE)
FS_FSTAB_DECLARE_ENTRY(PARTITION_NODE);
#else /* PARTITION_NODE */
#error Please declare the partition node lfs1. No usage of storage.
#endif /* PARTITION_NODE */

class Persistence {
 public:
  using ReadCallback = void (*)(void *data, uint16_t len_read, void *ctx);
  static Persistence &Instance() {
    static Persistence sInstance;
    return sInstance;
  }

  int init(bool eraseFlash);
  int deinit();
  int readFile(const char *path, void *buf, uint16_t len, ReadCallback cb, void *ctx);
  int writeFile(const char *path, void *data, uint16_t len, uint16_t offset, bool append);
  int deleteFile(const char *path);
  size_t getFileSize(const char *path);

 private:
  Persistence() {}
  ~Persistence() {}

  int increaseBootCounter();
  int lsdir(const char *path);
  int flash_info_and_erase(unsigned int id, bool eraseFlash);

  struct fs_mount_t *mp = &FS_FSTAB_ENTRY(PARTITION_NODE);
  bool mInitialized = false;
};