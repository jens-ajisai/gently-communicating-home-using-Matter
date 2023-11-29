#include "oob_transport_uart.h"

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

void OobTransportUart::RxHandlerEntry(const struct device *dev, void *user_data) {
  OobTransportUart::Instance().RxHandler(dev, user_data);
}

void OobTransportUart::RxHandler(const struct device *dev, void *user_data) {
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

      /* if queue is full, message is silently dropped */
      if (rx_msgq != nullptr) k_msgq_put(rx_msgq, &rx_buf, K_NO_WAIT);

      /* reset the buffer (it was copied to the msgq) */
      rx_buf_pos = 0;
    } else if (rx_buf_pos < (sizeof(rx_buf) - 1)) {
      rx_buf[rx_buf_pos++] = c;
    }
    /* else: characters beyond buffer size are dropped */
  }
}

void OobTransportUart::Send(char *buf) {
  int msg_len = strlen(buf);

  for (int i = 0; i < msg_len; i++) {
    uart_poll_out(dev, buf[i]);
  }
}

void OobTransportUart::Init(struct k_msgq *msgq) {
  if (!device_is_ready(dev)) {
    LOG_ERR("Transport device not found!");
    return;
  }

  rx_msgq = msgq;

  /* configure interrupt and callback to receive data */
  int ret = uart_irq_callback_user_data_set(dev, OobTransportUart::RxHandlerEntry, NULL);

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
}

void OobTransportUart::Deinit() {
  uart_irq_rx_disable(dev);
  rx_buf_pos = 0;
  rx_msgq = nullptr;  
}