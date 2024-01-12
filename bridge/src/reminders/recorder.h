#pragma once

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

// interface between main and recorder
struct work_with_data {
  struct k_work_delayable work;
  char path[32];
};

void recorder_init();
void start_recording(struct work_with_data* onRecordingFinishedWork);
void stop_recording();

#ifdef __cplusplus
}
#endif
