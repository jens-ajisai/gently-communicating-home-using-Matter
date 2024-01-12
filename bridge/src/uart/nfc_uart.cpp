#include "nfc_uart.h"

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

void NfcUart::RxHandlerEntry(const struct device *dev, void *user_data) {
  NfcUart::Instance().RxHandler(dev, user_data);
}

void NfcUart::RxHandler(const struct device *dev, void *user_data) {
  uint8_t c;

  if (!uart_irq_update(dev)) {
    return;
  }

  if (!uart_irq_rx_ready(dev)) {
    return;
  }

  /* read until FIFO empty */
  while (uart_fifo_read(dev, &c, 1) == 1) {
    if ((c == '\n') && rx_buf_pos > 0) {
      /* terminate string */
      rx_buf[rx_buf_pos] = '\0';

      // Workaround: For some reason this newline ends up in the rx_buf. It should not!
      rx_buf[rx_buf_pos - 1] = '\0';
      if (cb != nullptr) cb(rx_buf, rx_buf_pos);

      /* reset the buffer */
      rx_buf_pos = 0;
    } else if (rx_buf_pos < (sizeof(rx_buf) - 1)) {
      rx_buf[rx_buf_pos++] = c;
    }
    /* else: characters beyond buffer size are dropped */
  }
}

void NfcUart::Send(char *buf, uint16_t msg_len) {
  for (int i = 0; i < msg_len; i++) {
    uart_poll_out(dev, buf[i]);
  }
}

void NfcUart::SendEntry(char *buf) {
  int msg_len = strlen(buf);
  NfcUart::Instance().Send(buf, msg_len);
}

void NfcUart::SwitchPollModeEntry(k_timer *timer) {
  NfcUart::Instance().SwitchPollMode();
}

void NfcUart::SwitchPollMode() {
//  LOG_INF("Switch poll mode to oob == %d", pollOob);

  if(pollOob) {
    char command = CMD_READ_OOB;
    Send(&command, 1);
    pollOob = !pollOob;
  } else {
    char command = CMD_READ_FELICA;
    Send(&command, 1);
    pollOob = !pollOob;
  }

  k_timer_start(&switchPollModeTimer, K_SECONDS(switchPollInterval), K_NO_WAIT);  
}

void NfcUart::Init(UartMessageCallback callback) {
  LOG_INF("Init NfcUart.");

  if (!device_is_ready(dev)) {
    LOG_ERR("Transport device not found!");
    return;
  }

  cb = callback;

  /* configure interrupt and callback to receive data */
  int ret = uart_irq_callback_user_data_set(dev, NfcUart::RxHandlerEntry, NULL);

  if (ret < 0) {
    if (ret == -ENOTSUP) {
      LOG_ERR("Interrupt-driven UART API support not enabled\n");
    } else if (ret == -ENOSYS) {
      LOG_ERR("UART device does not support interrupt-driven API\n");
    } else {
      LOG_ERR("Error setting UART callback: %d\n", ret);
    }
    return;
  }
  uart_irq_rx_enable(dev);

  char command = CMD_ON;
  Send(&command, 1);

  k_timer_init(&switchPollModeTimer, &NfcUart::SwitchPollModeEntry, nullptr);
  k_timer_user_data_set(&switchPollModeTimer, this);
  k_timer_start(&switchPollModeTimer, K_SECONDS(switchPollInterval), K_NO_WAIT);
}

void NfcUart::Deinit() {
  uart_irq_rx_disable(dev);
  rx_buf_pos = 0;
  cb = nullptr;  
}
