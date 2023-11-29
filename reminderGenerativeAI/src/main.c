#include <zephyr/audio/dmic.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usb_device.h>

#include "ai/completions.h"
#include "ai/definitions.h"
#include "ai/whisper.h"
#include "button.h"
#include "cJSON.h"
#include "persistence/persistence.h"
#include "recorder.h"
#include "reminder.h"
LOG_MODULE_REGISTER(app, CONFIG_MAIN_LOG_LEVEL);

// Magic global variable that holds the results of the requests to open.ai
extern char response_buf[MAX_EXPECTED_RESPONSE_BODY];

static void parse_json(const char *data) {
  LOG_INF("Parsing completion result %s", data);

  cJSON *jsonRoot = cJSON_Parse(data);
  if (!cJSON_IsObject(jsonRoot)) {
    const char *error_ptr = cJSON_GetErrorPtr();
    if (error_ptr != NULL) {
      LOG_ERR("Error before: %s", error_ptr);
    }
    goto end;
  }

  cJSON *jsonRequest = cJSON_GetObjectItemCaseSensitive(jsonRoot, "request");
  if (!cJSON_IsString(jsonRequest)) {
    LOG_ERR("No request available");
    goto end;
  }
  const char *requestString = jsonRequest->valuestring;

  cJSON *jsonParameter = cJSON_GetObjectItemCaseSensitive(jsonRoot, "parameter");
  if (!cJSON_IsObject(jsonParameter)) {
    LOG_ERR("No parameter available");
    goto end;
  }

  cJSON *jsonName = cJSON_GetObjectItemCaseSensitive(jsonParameter, "name");
  if (!cJSON_IsString(jsonName)) {
    LOG_ERR("No name available");
    goto end;
  }
  const char *nameString = jsonName->valuestring;

  if (strcmp(requestString, "delete") == 0) {
    deleteReminder(nameString);
  } else if (strcmp(requestString, "add") == 0) {
    cJSON *jsonDue = cJSON_GetObjectItemCaseSensitive(jsonParameter, "due");
    if (!cJSON_IsString(jsonDue)) {
      LOG_ERR("No due available");
      goto end;
    }
    const char *dueString = jsonDue->valuestring;
    addReminder(nameString, dueString);
  }

end:
  cJSON_Delete(jsonRoot);
}

static void onRecordingFinished(struct k_work *work) {
  struct work_with_data *work_data = CONTAINER_OF(work, struct work_with_data, work);
  request_transcription(work_data->path);
  response_buf[strcspn(response_buf, "\n")] = 0;
  request_chat_completion(response_buf);
  parse_json(response_buf);
  LOG_INF("NEXT ALARM UNTIL %llu",checkdue());
}

int main(void) {
  int ret;

#if defined(CONFIG_USB_DEVICE_STACK)
  usb_enable(NULL);
#endif

  LOG_INF("App Start");

  k_work_init(&onRecordingFinishedWork.work, onRecordingFinished);

  fs_init(false);
  button_init();
  recorder_init();

  while (true) {
    LOG_INF("WAIT FOR BUTTON");
    k_poll(recordButtonEvents, ARRAY_SIZE(recordButtonEvents), K_FOREVER);
    if (recordButtonEvents[0].signal->result == BUTTON_PUSHED) {
      k_poll_signal_reset(&recordButtonSignal);
      LOG_INF("START RECORDING");

      ret = do_pdm_transfer();
      if (ret < 0) {
        return 0;
      }

      LOG_INF("STOP RECORDING");
    }
  }

  LOG_INF("Exiting");
  return 0;
}
