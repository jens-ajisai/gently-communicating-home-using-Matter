/*
 * Copyright (c) 2018 Jan Van Winkel <jan.van_winkel@dxplore.eu>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(display, CONFIG_CHIP_APP_LOG_LEVEL);

#ifdef CONFIG_LVGL
#include <lvgl.h>
static lv_obj_t *label_build;
static lv_obj_t *label_transcription;
static lv_obj_t *label_completions;

int display_init(void) {
  const struct device *display_dev;

  display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
  if (!device_is_ready(display_dev)) {
    LOG_ERR("Device not ready, aborting test");
    return 0;
  }

  label_build = lv_label_create(lv_scr_act());
  lv_label_set_text(label_build, __DATE__ " " __TIME__);
  lv_obj_align(label_build, LV_ALIGN_TOP_LEFT, 0, 0);

  label_transcription = lv_label_create(lv_scr_act());
  lv_label_set_long_mode(label_transcription, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(label_transcription, 240);
  lv_label_set_text(label_transcription, "");
  lv_obj_align(label_transcription, LV_ALIGN_LEFT_MID, 0, 0);

  label_completions = lv_label_create(lv_scr_act());
  lv_label_set_long_mode(label_completions, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(label_completions, 240);
  lv_label_set_text(label_completions, "");
  lv_obj_align(label_completions, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  lv_task_handler();
  display_blanking_off(display_dev);

  return 0;
}

void display_updateTranscription(const char *text) {
  lv_label_set_text(label_transcription, text);
  lv_task_handler();
}
void display_updateCompletions(const char *text) {
  lv_label_set_text(label_completions, text);
  lv_task_handler();
}
#else
int display_init(void) { return 0; }
void display_updateTranscription(const char* text) {}
void display_updateCompletions(const char* text) {}
#endif

/*

// A try to use k_malloc for lvgl

void* display_malloc(size_t size) {
  void* ptr = k_malloc(size);
  if (!ptr) LOG_ERR("Error out of memory");
  return ptr;
}

void display_free(void* ptr) {
  if (!ptr) LOG_ERR("Free of NULL!");
  k_free(ptr);
}

// https://codereview.stackexchange.com/questions/151019/implementing-realloc-in-c
void* display_realloc(void* ptr, size_t new_size) {
  if (new_size == 0) {
    display_free(ptr);
    return NULL;
  } else if (!ptr) {
    return display_malloc(new_size);
  } else {
    void* ptrNew = display_malloc(new_size);
    if (ptrNew) {
      memcpy(ptrNew, ptr, new_size);
      display_free(ptr);
    }
    return ptrNew;
  }
}

*/