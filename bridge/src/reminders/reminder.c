
#include "reminder.h"

#include <date_time.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "persistence/persistence.h"
LOG_MODULE_REGISTER(reminder, CONFIG_CHIP_APP_LOG_LEVEL);

#define REMINDERS_FILE "reminder"
#define TIME_ZONE_SHIFT (9 * 60 * 60)
#define DATE_STRING_LEN (17)
#define DATE_STRING_START_OF_HOUR (11)
#define NAME_STRING_MAX_LEN (24)
#define REMINDERS_MAX_NUM (4)

struct reminder {
  char name[NAME_STRING_MAX_LEN];
  int64_t dueDate;
  bool daily;
};

static struct reminder reminders[REMINDERS_MAX_NUM];

static bool initialized = false;

static void persistReminders() {
  fs_overwriteData(REMINDERS_FILE, reminders, sizeof(reminders), 0);
}

static const char *reminder_util_formatDate(int64_t t) {
  static char formattedDate[DATE_STRING_LEN] = {0};
  struct tm *tm_localtime;
  tm_localtime = localtime(&t);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
  snprintf(formattedDate, sizeof(formattedDate), "%4d-%02d-%02d %02d:%02d",
           tm_localtime->tm_year + 1900, tm_localtime->tm_mon + 1, tm_localtime->tm_mday,
           tm_localtime->tm_hour, tm_localtime->tm_min);
#pragma GCC diagnostic pop
  return formattedDate;
}

static int64_t parseTime(const char *dueDate) {
  // https://stackoverflow.com/questions/2722606/how-to-parse-a-string-into-a-datetime-struct-in-c
  struct tm *parsedTime;
  int hour, minute, month, day, year;
  if (sscanf(dueDate, "%d-%d-%d %d:%d", &year, &month, &day, &hour, &minute) != EOF) {
    int64_t now = -1;
    int rc = date_time_now(&now);
    if (rc >= 0) {
      now = now / 1000 + TIME_ZONE_SHIFT;
      parsedTime = localtime(&now);

      // tm_months is months since 1900
      parsedTime->tm_year = year - 1900;
      // tm_months is months since january
      parsedTime->tm_mon = month - 1;
      parsedTime->tm_mday = day;
      parsedTime->tm_hour = hour;
      parsedTime->tm_min = minute;
      int64_t t = mktime(parsedTime);
      LOG_DBG("parse time %s to %lld", dueDate, t);
      return mktime(parsedTime);
    }
  }
  return -1;
}

void reminder_add(const char *name, const char *dueDate, bool daily) {
  if(!initialized) reminder_init();
  if (initialized) {
    // if the reminder already exists, update it.
    for (size_t i = 0; i < ARRAY_SIZE(reminders); i++) {
      if (strcmp(reminders[i].name, name) == 0) {
        int64_t parsedDueDate = parseTime(dueDate);
        if(parsedDueDate != -1) {
          reminders[i].dueDate = parsedDueDate;
          reminders[i].daily = daily;

          LOG_INF("Updated reminder %s at %s (%lld), daily=%d to slot %d", reminders[i].name,
                  reminder_util_formatDate(reminders[i].dueDate), reminders[i].dueDate, daily, i);

          persistReminders();
        }
        return;
      }
    }

    for (size_t i = 0; i < ARRAY_SIZE(reminders); i++) {
      if (reminders[i].name[0] == 0) {
        int64_t parsedDueDate = parseTime(dueDate);
        if(parsedDueDate != -1) {
          strncpy(reminders[i].name, name, 50);
          reminders[i].dueDate = parsedDueDate;
          reminders[i].daily = daily;

          LOG_INF("Added reminder %s at %s (%lld), daily=%d to slot %d", reminders[i].name,
                  reminder_util_formatDate(reminders[i].dueDate), reminders[i].dueDate, daily, i);

          persistReminders();
        }
        return;
      }
    }
  }
}

void reminder_delete(const char *name) {
  if(!initialized) reminder_init();
  if (initialized) {
    for (size_t i = 0; i < ARRAY_SIZE(reminders); i++) {
      if (strcmp(reminders[i].name, name) == 0) {
        LOG_INF("Delete reminder %s at %s (%lld) from slot %d", reminders[i].name,
                reminder_util_formatDate(reminders[i].dueDate), reminders[i].dueDate, i);
        int64_t newDueDate = reminders[i].dueDate + 60 * 60 * 24;
        bool wasDaily = reminders[i].daily;

        if (wasDaily) {
          int64_t now = -1;
          int rc = date_time_now(&now);
          if (rc >= 0) {
            now = now / 1000 + TIME_ZONE_SHIFT;
            while (newDueDate < now) {
              LOG_INF("  ... newDueDate < now (%lld < %lld)", newDueDate, now);
              newDueDate += (60 * 60 * 24);
            }
          }
        }

        reminders[i].name[0] = 0;
        reminders[i].dueDate = 0;
        reminders[i].daily = false;

        if (wasDaily) {
          LOG_INF("  ... new due date %s (%lld)", reminder_util_formatDate(newDueDate), newDueDate);
          reminder_add(name, reminder_util_formatDate(newDueDate), wasDaily);
        } else {
          // in case of daily, timer is persisted inside reminder_add
          persistReminders();
        }

        break;
      }
    }
  }
}

int64_t reminder_checkDue() {
  if(!initialized) reminder_init();  
  int64_t timeUntilNextDue = LONG_MAX;
  if (initialized) {  
    int64_t now = -1;
    int rc = date_time_now(&now);
    if (rc >= 0) {
      now = now / 1000 + TIME_ZONE_SHIFT;

      for (size_t i = 0; i < ARRAY_SIZE(reminders); i++) {
        if (reminders[i].name[0] != 0) {
          int64_t remainingTime = reminders[i].dueDate - now;
          LOG_INF("Check reminder %s at %s (%lld), remaining %lld", reminders[i].name,
                  reminder_util_formatDate(reminders[i].dueDate), reminders[i].dueDate,
                  remainingTime);
          timeUntilNextDue = MIN(timeUntilNextDue, remainingTime);
        }
      }
      // If there is no counter return a large value but not LONG_MAX as this is the error code
      if(timeUntilNextDue == LONG_MAX) timeUntilNextDue = LONG_MAX - 1;
    }
  }
  LOG_INF("Check reminder result remaining %lld", timeUntilNextDue);
  return timeUntilNextDue;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
static const char* reminder_json_fmt = 
  "{\\\"name\\\": \\\"%s\\\", \\\"due\\\": \\\"%s\\\", \\\"daily\\\": %s}\\n";

const char* reminder_printJson() {
  // actually it should be 65 + NAME_STRING_MAX_LEN + Null terminator
  static char remindersJsonFormatted[REMINDERS_MAX_NUM * (NAME_STRING_MAX_LEN + 74)] = {0};
  memset(remindersJsonFormatted, 0, sizeof(remindersJsonFormatted));
  if(!initialized) reminder_init();
  if (initialized) {
    for (size_t i = 0; i < ARRAY_SIZE(reminders); i++) {
      if (reminders[i].name[0] != 0) {
        char reminderJsonTmp[(NAME_STRING_MAX_LEN + 74)] = {0};
        snprintf(reminderJsonTmp, NAME_STRING_MAX_LEN + 74, reminder_json_fmt, reminders[i].name,
                reminder_util_formatDate(reminders[i].dueDate), reminders[i].daily ? "true" : "false");
        strcat(remindersJsonFormatted, reminderJsonTmp);
      }
    }
  }
  return remindersJsonFormatted;
}
#pragma GCC diagnostic pop        

void reminder_print() {
  if(!initialized) reminder_init();
  if (initialized) {
    for (size_t i = 0; i < ARRAY_SIZE(reminders); i++) {
      if (reminders[i].name[0] != 0) {
        LOG_INF("Reminder %s at %s (%lld) daily=%d", reminders[i].name,
                reminder_util_formatDate(reminders[i].dueDate), reminders[i].dueDate,
                reminders[i].daily);
      }
    }
  }
  LOG_INF("%s",reminder_printJson());
}

const char *reminder_util_currentDateTime() {
  if(!initialized) reminder_init();
  if (initialized) {
    int64_t now = 0;
    int rc = date_time_now(&now);
    if (rc == 0) {
      now = now / 1000 + TIME_ZONE_SHIFT;
      return reminder_util_formatDate(now);
    }
  }
  static char unknownDate[DATE_STRING_LEN] = "unknown";
  return unknownDate;
}

#define MAX_COUNT (9)

void reminder_init() {
  date_time_update_async(NULL);

  int count = 0;
  int64_t now = 0;
  int rc = date_time_now(&now);
  while (rc != 0) {
    k_sleep(K_MSEC(300));
    rc = date_time_now(&now);
    if (count >= MAX_COUNT) break;
    count++;
  }

  if (rc == 0) {
    int readBytes = fs_readFile(REMINDERS_FILE, reminders, sizeof(reminders), NULL, NULL);
    initialized = true;

    // If there is no persisted file or loading fails, default to daily homework at 18:30
    if (readBytes != sizeof(reminders)) {
      memset(reminders, 0, sizeof(reminders));
      const char *currentDate = reminder_util_currentDateTime();
      char newDate[DATE_STRING_LEN] = {0};
      memcpy(newDate, currentDate, DATE_STRING_LEN);
      sprintf(&newDate[DATE_STRING_START_OF_HOUR], "18:30");
      reminder_add("homework", newDate, true);
    }

    now = now / 1000 + TIME_ZONE_SHIFT;
    for (size_t i = 0; i < ARRAY_SIZE(reminders); i++) {
      if (reminders[i].name[0] != 0 && reminders[i].dueDate < now && reminders[i].daily) {
        // reminders[i].name will be set to 0 inside delete function 
        // and is empty when adding the new daily reminder in add function
        char toBeDeletedReminder[NAME_STRING_MAX_LEN] = {0};
        memcpy(toBeDeletedReminder, reminders[i].name, NAME_STRING_MAX_LEN);
        reminder_delete(toBeDeletedReminder);
      }
    }
    LOG_INF("Reminders initialized");
    reminder_print();
  }
}

/*
Testing OK

Initialize reminders
Initialize reminders again and check for updates
Initialize reminders again and check for nothing new
Add reminder
Delete reminder
Update reminder with daily reminder
Delete daily reminder
Delete daily reminder again
Check reminder in future
Add past reminder
Check reminder expired
Check reminder for multiple entries
Check reminder for 0 entries
Add malformatted string
Delete non-existent item

reminders init

*/