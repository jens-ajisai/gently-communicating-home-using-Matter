#pragma once

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

int request_transcription(const char *path);

#ifdef __cplusplus
}
#endif
