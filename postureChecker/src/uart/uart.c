#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "../bt.h"
LOG_MODULE_DECLARE(app, CONFIG_MAIN_LOG_LEVEL);

#define UART_DEVICE_NODE DT_NODELABEL(uart0)

static const struct device *const transport_dev = DEVICE_DT_GET(UART_DEVICE_NODE);
static uint16_t rx_buf_pos;
static char rx_buf[OOB_MESSAGE_LEN];
char local_oob_message[OOB_MESSAGE_LEN];

K_MSGQ_DEFINE(transportMsgq, OOB_MESSAGE_LEN, 2, 4);

void transport_send(char *buf) {
  int msg_len = strlen(buf);

  for (int i = 0; i < msg_len; i++) {
    uart_poll_out(transport_dev, buf[i]);
  }
}

void transport_rx_handler(const struct device *dev, void *user_data) {
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
      k_msgq_put(&transportMsgq, &rx_buf, K_NO_WAIT);

      /* reset the buffer (it was copied to the msgq) */
      rx_buf_pos = 0;
    } else if (rx_buf_pos < (sizeof(rx_buf) - 1)) {
      rx_buf[rx_buf_pos++] = c;
    }
    /* else: characters beyond buffer size are dropped */
  }
}

void module_thread_fn() {
  if (!device_is_ready(transport_dev)) {
    LOG_ERR("Transport device not found!");
    return;
  }

  /* configure interrupt and callback to receive data */
  int ret = uart_irq_callback_user_data_set(transport_dev, transport_rx_handler, NULL);

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
  uart_irq_rx_enable(transport_dev);

  LOG_INF("Start listening for remote oob data.");

  char tx_buf[OOB_MESSAGE_LEN] = {0};
  while (k_msgq_get(&transportMsgq, &tx_buf, K_FOREVER) == 0) {
    LOG_INF("Received remote oob data: %s", tx_buf);

    char *addr = strtok(tx_buf, " ");
    if (addr == NULL) continue;
    char *type = strtok(NULL, " ");
    if (type == NULL) continue;
    char *r = strtok(NULL, " ");
    if (r == NULL) continue;
    char *c = strtok(NULL, " ");
    if (c == NULL) continue;

    int err = set_remote_oob(addr, type, r, c);
    if (err) continue;

    local_oob_get(local_oob_message, sizeof(local_oob_message));
    LOG_INF("Send to UART: %s", local_oob_message);
    transport_send(local_oob_message);

    uart_irq_rx_disable(transport_dev);

    return;
  }
}

K_THREAD_DEFINE(bt_oob_transport_thread, 1024, module_thread_fn, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);