#include "nfc_task.h"
#include "nfc_helper.h"

#include "pico/util/queue.h"

#include <stdio.h>
#include <string.h>

#include "logging.h"

#define SEND_BUF_LEN (256)

static enum states state = BOOT;

static queue_t* cmd_queue;
static send_frame_t send_frame_f;

void nfc_task_init(queue_t* queue, send_frame_t send_frame) {
  send_frame_f = send_frame;
  cmd_queue = queue;
}

void nfc_task() {
  static enum commands current_command = CMD_COMPLETE;
  static uint8_t send_buf[SEND_BUF_LEN];

  // check for a new command
  if (current_command == CMD_COMPLETE) {
    enum commands next_command;
    bool removed = true;
    while(removed) {
      removed = queue_try_remove(cmd_queue, &next_command);
      if (next_command >= CMD_ON && next_command <= CMD_OFF) {
        current_command = next_command;
        if (current_command != CMD_COMPLETE) logging("Got next command %c \r\n", current_command);
        break;
      }
    }
  }

  // process current command
  uint16_t send_buf_len = 0;
  if (current_command == CMD_READ_FELICA) {
    logging("process CMD_READ_FELICA\r\n");
    state = WAIT_FOR_RESPONSE;
    send_buf_len = listenPassiveTargetFelica(send_buf, SEND_BUF_LEN);
    send_frame_f(send_buf, send_buf_len);
    current_command = CMD_COMPLETE;
  } else if (current_command == CMD_READ_OOB) {
    logging("process CMD_READ_OOB\r\n");
    state = WAIT_FOR_RESPONSE;
    send_buf_len = listenPassiveTargetNfcTypeA(send_buf, SEND_BUF_LEN);
    send_frame_f(send_buf, send_buf_len);
    current_command = CMD_COMPLETE;
  } else if (current_command == CMD_OFF) {
    logging("process CMD_OFF\r\n");
    state = WAIT_FOR_RESPONSE;
    send_buf_len = rfOff(send_buf, SEND_BUF_LEN);
    send_frame_f(send_buf, send_buf_len);
    current_command = CMD_COMPLETE;
  } else if (current_command == CMD_ON) {
    logging("process CMD_ON\r\n");
    state = WAIT_FOR_RESPONSE;
    send_buf_len = reset(send_buf, SEND_BUF_LEN);
    send_frame_f(send_buf, send_buf_len);
    current_command = CMD_ONGOING;
  }

  // configure the device after reset on command ON
  switch (state) {
    case RESET: {
      logging("state RESET\r\n");
      state = WAIT_FOR_RESPONSE;
      send_buf_len = getFirmwareVersion(send_buf, SEND_BUF_LEN);
      send_frame_f(send_buf, send_buf_len);
      break;
    }
    case FIRMWARE_CHECKED: {
      logging("state FIRMWARE_CHECKED\r\n");
      state = WAIT_FOR_RESPONSE;
      send_buf_len = configureWaitTime(send_buf, SEND_BUF_LEN);
      send_frame_f(send_buf, send_buf_len);
      current_command = CMD_COMPLETE;
      break;
    }
    default: {
      break;
    }
  }

  // read oob when device detected on command CMD_READ_OOB
  switch (state) {
    case NFC_DETECTED: {
      logging("state NFC_DETECTED\r\n");
      state = WAIT_FOR_RESPONSE;
      send_buf_len = readDataNfcTypeA(send_buf, SEND_BUF_LEN);
      send_frame_f(send_buf, send_buf_len);
      break;
    }
    default: {
      break;
    }
  }

  // continuously read on command CMD_READ_FELICA and CMD_READ_OOB
  switch (state) {
    case FELICA_DETECTED: {
      logging("state FELICA_DETECTED\r\n");
      state = WAIT_FOR_RESPONSE;
      send_buf_len = listenPassiveTargetFelica(send_buf, SEND_BUF_LEN);
      send_frame_f(send_buf, send_buf_len);
      break;
    }
    case OOB_TRANSFERED: {
      logging("state OOB_TRANSFERED\r\n");
      state = WAIT_FOR_RESPONSE;
      send_buf_len = listenPassiveTargetNfcTypeA(send_buf, SEND_BUF_LEN);
      send_frame_f(send_buf, send_buf_len);
    }
    case NFC_DETECTED:
    default: {
      break;
    }
  }
}

void nfc_handleMessage(uint8_t* buf, uint16_t len) {
  if (state == GOT_ACK && buf[5] == RES_CODE) {
    if (buf[6] == RES_RESET_MODE) {
      state = RESET;
    } else if (buf[6] == RES_GET_FIRMWARE_VERSION) {
      state = FIRMWARE_CHECKED;
    } else if (buf[6] == RES_RF_CONFIGURATION) {
      state = CONFIGURED;
    } else if (len == 27 && buf[6] == RES_IN_LIST_PASSIVE_TARGET) {
      state = NFC_DETECTED;
    } else if (len == 29 && buf[6] == RES_IN_LIST_PASSIVE_TARGET) {
      if (buf[0x11] == 0x3D && buf[0x12] == 0x65) {
        printf("ID:Jinbei\n");
      } else if (buf[0x11] == 0x3F && buf[0x12] == 0x47) {
        printf("ID:Lucky Cat\n");
      } else if (buf[0x11] == 0x1B && buf[0x12] == 0x7B) {
        printf("ID:Meow\n");
      } else if (buf[0x11] == 0x40 && buf[0x12] == 0x58) {
        printf("ID:Flower\n");
      } else if (buf[0x11] == 0x3F && buf[0x12] == 0x67) {
        printf("ID:Snowflake\n");
      } else if (buf[0x11] == 0x3C && buf[0x12] == 0x96) {
        printf("ID:Fish\n");
      } else if (buf[0x11] == 0x1A && buf[0x12] == 0x7C) {
        printf("ID:Jelly Fish\n");
      } else if (buf[0x11] == 0x40 && buf[0x12] == 0x48) {
        printf("ID:Butterfly\n");
      } else if (buf[0x11] == 0x3F && buf[0x12] == 0x57) {
        printf("ID:Sakura\n");
      }
      state = FELICA_DETECTED;
    } else if (buf[6] == RES_IN_DATA_EXCHANGE) {
      // no error
      if (buf[7] == 0x00) {
        static char oob_data[OOB_MESSAGE_LEN];
        parseNfcNdefLeOob(buf, len, oob_data, OOB_MESSAGE_LEN);
        printf("%s\n", oob_data);
      }
      state = OOB_TRANSFERED;
    }
  }

  static uint8_t ack[] = ACK;
  if (memcmp(buf, ack, sizeof(ack)) == 0) {
    state = GOT_ACK;
  }
  logging("state = %d\r\n", state);
}
