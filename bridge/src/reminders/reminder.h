#pragma once

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

void reminder_init();
void reminder_print();

void reminder_add(const char *name, const char *dueDate, bool daily);
void reminder_delete(const char *name);
int64_t reminder_checkDue();

const char *reminder_util_currentDateTime();
const char *reminder_printJson();

#ifdef __cplusplus
}
#endif