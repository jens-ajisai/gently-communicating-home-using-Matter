#pragma once

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*feedback_text_t)(const char *data);

void addReminder(const char *name, const char *dueDate, bool daily);
void deleteReminder(const char *name);
int64_t checkdue();
void printReminders();

void initRemindersApp(feedback_text_t f1, feedback_text_t f2);
void startAiFlow();
void stopRecording();

void cardTriggerAction(const char *name);

#ifdef __cplusplus
}
#endif
