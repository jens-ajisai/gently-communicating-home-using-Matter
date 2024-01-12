#include "reminders_app.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ai/completions.h"
#include "ai/definitions.h"
#include "ai/whisper.h"
#include "cJSON.h"
#include "persistence/persistence.h"
#include "recorder.h"
#include "reminder.h"

LOG_MODULE_REGISTER(reminders_main, CONFIG_CHIP_APP_LOG_LEVEL);

static struct work_with_data onRecordingFinishedWork;
static feedback_text_t feedback_completions_cb;
static feedback_text_t feedback_transcription_cb;

// Magic global variable that holds the results of the requests to open.ai
extern char response_buf[MAX_EXPECTED_RESPONSE_BODY];

static void parse_json(const char *data) {
  LOG_INF("Parsing completion result %s", data);

  cJSON *jsonRoot = cJSON_Parse(data);
  do {
    if (!cJSON_IsObject(jsonRoot)) {
      const char *error_ptr = cJSON_GetErrorPtr();
      if (error_ptr != NULL) {
        LOG_ERR("Error before: %s", error_ptr);
      }
      LOG_ERR("Failed to parse JSON");      
      break;
    }

    cJSON *jsonRequest = cJSON_GetObjectItemCaseSensitive(jsonRoot, "request");
    if (!cJSON_IsString(jsonRequest)) {
      LOG_ERR("No request available");
      break;
    }
    const char *requestString = jsonRequest->valuestring;

    cJSON *jsonParameter = cJSON_GetObjectItemCaseSensitive(jsonRoot, "parameter");
    if (!cJSON_IsObject(jsonParameter)) {
      LOG_ERR("No parameter available");
      break;
    }

    cJSON *jsonName = cJSON_GetObjectItemCaseSensitive(jsonParameter, "name");
    if (!cJSON_IsString(jsonName)) {
      LOG_ERR("No name available");
      break;
    }
    const char *nameString = jsonName->valuestring;

    if (strcmp(requestString, "delete") == 0) {
      reminder_delete(nameString);
    } else if (strcmp(requestString, "add") == 0) {
      cJSON *jsonDue = cJSON_GetObjectItemCaseSensitive(jsonParameter, "due");
      if (!cJSON_IsString(jsonDue)) {
        LOG_ERR("No due available");
        break;
      }
      const char *dueString = jsonDue->valuestring;
      reminder_add(nameString, dueString, false);
      return;
    }
  } while (0);

  cJSON_Delete(jsonRoot);
}


static void onRecordingFinished(struct k_work *work) {
  // Retrieve the path of the audio recording
  struct work_with_data *work_data = CONTAINER_OF(work, struct work_with_data, work);
  LOG_INF("onRecordingFinished path=%s", work_data->path);

  // Get the transcription of the recording
  request_transcription(work_data->path);

  // Terminates the transcription at the first newline and removes it.
  response_buf[strcspn(response_buf, "\n")] = 0;

  if(feedback_transcription_cb) feedback_transcription_cb(response_buf);

  // Create a request with the current date and the request string.
  const char *request[3] = {reminder_util_currentDateTime(), reminder_printJson(), response_buf};
  request_chat_completion(request);

  // Parse the result. This adds a reminder if parsing was successful.
  // Otherwise ignores the result.
  parse_json(response_buf);

  if(feedback_completions_cb) feedback_completions_cb(response_buf);

  // Check for the next due. It gives the new alarm if it is next.
  LOG_INF("NEXT ALARM UNTIL %llu", reminder_checkDue());
}

void initRemindersApp(feedback_text_t f1, feedback_text_t f2) {
  k_work_init_delayable(&onRecordingFinishedWork.work, onRecordingFinished);
  feedback_transcription_cb = f1;
  feedback_completions_cb = f2;
  fs_init(false);
  recorder_init();
  reminder_init();
}

void startAiFlow() { start_recording(&onRecordingFinishedWork); }

void stopRecording() { stop_recording(); }

void addReminder(const char *name, const char *dueDate, bool daily) { reminder_add(name, dueDate, daily); }

void deleteReminder(const char *name) { reminder_delete(name); }

int64_t checkdue() { return reminder_checkDue(); }

void printReminders() {
  reminder_print();
}

void cardTriggerAction(const char *name) {
  if(strncmp(name, "ID:Fish", 7) == 0) {
    deleteReminder("homework");
  }
}