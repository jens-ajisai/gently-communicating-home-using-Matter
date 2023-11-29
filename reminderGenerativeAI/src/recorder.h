#pragma once

#include <zephyr/kernel.h>

struct work_with_data {
  struct k_work work;
  char path[32];
};

extern struct work_with_data onRecordingFinishedWork;

int do_pdm_transfer();
void recorder_init();