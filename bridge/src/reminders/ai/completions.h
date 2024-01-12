#pragma once

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

int request_chat_completion(const char **request);

#ifdef __cplusplus
}
#endif