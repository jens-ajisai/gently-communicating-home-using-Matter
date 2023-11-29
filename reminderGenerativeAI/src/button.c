#include "button.h"

#include <dk_buttons_and_leds.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(button, CONFIG_MAIN_LOG_LEVEL);

#define RECORD_BUTTON_MASK DK_BTN1_MSK

struct k_poll_signal recordButtonSignal;
struct k_poll_event recordButtonEvents[1];

void ButtonEventHandler(uint32_t buttonState, uint32_t hasChanged) {
  if (RECORD_BUTTON_MASK & hasChanged) {
    if (RECORD_BUTTON_MASK & buttonState) {
      // pushed
      k_poll_signal_raise(&recordButtonSignal, BUTTON_PUSHED);
    } else {
      // released
      k_poll_signal_raise(&recordButtonSignal, BUTTON_RELEASED);
    }
  }
}

int button_init() {
  /* Initialize buttons */
  int ret = dk_buttons_init(ButtonEventHandler);
  if (ret) {
    LOG_ERR("dk_buttons_init() failed");
    return ret;
  }

  k_poll_signal_init(&recordButtonSignal);
  k_poll_event_init(recordButtonEvents, K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY,
                    &recordButtonSignal);

  return 0;
}