#pragma once

#include <zephyr/kernel.h>

#define TIME_ZONE_SHIFT (9 * 60 * 60)

struct reminder {
  char name[50];
  int64_t dueDate;
};

void addReminder(const char *name, const char *dueDate);
void deleteReminder(const char *name);
int64_t checkdue();