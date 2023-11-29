#pragma once

#include "oob_exchange_manager.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>

// Better use a chosen value that has to be defined in the overlay.
#ifdef CONFIG_CHIP_WIFI
#define UART_DEVICE_NODE DT_NODELABEL(uart1)
#else
#define UART_DEVICE_NODE DT_NODELABEL(uart2)
#endif

class OobTransportUart : public OobTransport {
 public:
  static OobTransportUart &Instance() {
    static OobTransportUart sInstance;
    return sInstance;
  }

  void Init(struct k_msgq * msgq);
  void Deinit();
  void Send(char *buf);
  void RxHandler(const struct device *dev, void *user_data);
  static void RxHandlerEntry(const struct device *dev, void *user_data);
 private:
  const struct device *const dev = DEVICE_DT_GET(UART_DEVICE_NODE);
};
