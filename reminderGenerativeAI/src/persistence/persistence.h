#pragma once

#include <zephyr/kernel.h>

typedef void (*fs_read_cb_t)(void *data, uint16_t len_read, void* ctx);


int fs_lsdir(const char *path);
int fs_appendData(const char *path, void *data, uint16_t len);
int fs_overwriteData(const char *path, void *data, uint16_t len, uint16_t offset);
int fs_readFile(const char *path, void *buf, uint16_t len, fs_read_cb_t cb, void* ctx);
size_t fs_getFileSize(const char *path);
int fs_deleteFile(const char *path);
int fs_init(bool eraseFlash);
int fs_deinit(void);