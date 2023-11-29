#pragma once

#include <zephyr/kernel.h>

enum buttonEvent { BUTTON_PUSHED, BUTTON_RELEASED };

extern struct k_poll_signal recordButtonSignal;
extern struct k_poll_event recordButtonEvents[1];

int button_init();