
#include "reminder.h"

#include <date_time.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(reminder, CONFIG_MAIN_LOG_LEVEL);

struct reminder reminders[10];

int64_t parseTime(const char *dueDate) {
  // https://stackoverflow.com/questions/2722606/how-to-parse-a-string-into-a-datetime-struct-in-c
  struct tm *parsedTime;
  int hour, minute, month, day;
  if (sscanf(dueDate, "%d-%d %d:%d", &month, &day, &hour, &minute) != EOF) {
    time_t rawTime = 0;
    parsedTime = localtime(&rawTime);

    // tm_year is years since 1900
    parsedTime->tm_year = 2023 - 1900;
    // tm_months is months since january
    parsedTime->tm_mon = month - 1;
    parsedTime->tm_mday = day;
    parsedTime->tm_hour = hour;
    parsedTime->tm_min = minute;
    return mktime(parsedTime);
  }
  return -1;
}

void addReminder(const char *name, const char *dueDate) {
  for (size_t i = 0; i < ARRAY_SIZE(reminders); i++) {
    if (reminders[i].name[0] == 0) {
      strncpy(reminders[i].name, name, 50);
      reminders[i].dueDate = parseTime(dueDate);
      LOG_INF("Added reminder %s at %llu", reminders[i].name, reminders[i].dueDate);
      break;
    }
  }
}

void deleteReminder(const char *name) {
  for (size_t i = 0; i < ARRAY_SIZE(reminders); i++) {
    if (strcmp(reminders[i].name, name) == 0) {
      LOG_INF("Delete reminder %s at %llu", reminders[i].name, reminders[i].dueDate);
      reminders[i].name[0] = 0;
      break;
    }
  }
}

int64_t checkdue() {
  int64_t timeUntilNextDue = LONG_MAX;
  int64_t now = -1;
  int rc = date_time_now(&now);
  if (rc >= 0) {
    now = now / 1000 + TIME_ZONE_SHIFT;

    for (size_t i = 0; i < ARRAY_SIZE(reminders); i++) {
      if (reminders[i].name[0] != 0) {
        int64_t remainingTime = reminders[i].dueDate - now;
        LOG_INF("Check reminder %s at %llu, remaining %llu", reminders[i].name, reminders[i].dueDate,
                remainingTime);
        timeUntilNextDue = MIN(timeUntilNextDue, remainingTime);
      }
    }
  }
  return timeUntilNextDue;
}