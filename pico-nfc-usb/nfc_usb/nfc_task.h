#pragma once

#include <stdint.h>

#include "pico/util/queue.h"

enum states {
  BOOT,
  RESET,
  FIRMWARE_CHECKED,
  CONFIGURED,
  FELICA_DETECTED,
  NFC_DETECTED,
  OOB_TRANSFERED,
  WAIT_FOR_RESPONSE,
  GOT_ACK,
};

enum commands {
  CMD_ON = 1,
  CMD_READ_FELICA,
  CMD_READ_OOB,
  CMD_OFF,
  CMD_ONGOING,
  CMD_COMPLETE,
  CMD_DUMMY
};

typedef void (*send_frame_t)(uint8_t* send_buf, uint16_t send_buf_len);

void nfc_task_init(queue_t* queue, send_frame_t send_frame);
void nfc_task();
void nfc_handleMessage(uint8_t* buf, uint16_t len);