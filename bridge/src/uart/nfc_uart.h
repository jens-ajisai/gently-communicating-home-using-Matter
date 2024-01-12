#pragma once

#include <zephyr/kernel.h>
#include <zephyr/device.h>

// only for OobExchangeManager::oobMessageLen
#include "../bridge/oob_exchange_manager.h"

#define UART_DEVICE_NODE DT_ALIAS(nfc_uart)

class NfcUart {
 public:
  static constexpr uint16_t switchPollInterval = 1;
 
  using UartMessageCallback = void (*)(char *buf, uint16_t len);                  

  enum commands {
    CMD_ON = 1,
    CMD_READ_FELICA,
    CMD_READ_OOB,
    CMD_OFF
  };

  static NfcUart &Instance() {
    static NfcUart sInstance;
    return sInstance;
  }
  void Init(UartMessageCallback cb);
  void Deinit();
  void Send(char *buf, uint16_t msg_len);

  // public because uart callback must be an accessible free or static function.
  static void RxHandlerEntry(const struct device *dev, void *user_data);
  static void SendEntry(char *buf);

  static void SwitchPollModeEntry(k_timer *timer);
  void SwitchPollMode();
 private:
  void RxHandler(const struct device *dev, void *user_data);

  const struct device *const dev = DEVICE_DT_GET(UART_DEVICE_NODE);

  uint16_t rx_buf_pos; 
  char rx_buf[OobExchangeManager::oobMessageLen];
  UartMessageCallback cb;

  bool pollOob;
  k_timer switchPollModeTimer;  
};
