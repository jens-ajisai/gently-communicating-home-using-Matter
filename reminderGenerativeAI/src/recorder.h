#pragma once

#include <zephyr/kernel.h>

// interface between main and recorder
struct work_with_data {
  struct k_work_delayable work;
  char path[32];
};

extern struct work_with_data onRecordingFinishedWork;

int do_pdm_transfer();
