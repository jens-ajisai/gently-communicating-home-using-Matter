#pragma once

#include <bluetooth/scan.h>
#include <lib/core/CHIPError.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>

class OobTransport;

class OobExchangeManager {
 public:
  static constexpr uint16_t keyStrLen = 33;
  static constexpr uint16_t oobMessageLen = (BT_ADDR_LE_STR_LEN + keyStrLen + keyStrLen + 1);

  static OobExchangeManager &Instance() {
    static OobExchangeManager sInstance;
    return sInstance;
  }

  static void AuthPairingOobDataRequestEntry(struct bt_conn *conn, struct bt_conn_oob_info *oob_info);
  void AuthPairingOobDataRequest(struct bt_conn *conn, struct bt_conn_oob_info *oob_info);

  void ExchangeOob(OobTransport *transport);

  // Public for shell access
  void SetRemoteOob(const char *addr, const char *type, const char *r, const char *c);
  void GetLocalOob(char *buffer = nullptr, uint16_t size = 0);

 private:
  void PrintLeOob(struct bt_le_oob *oob, char *buffer = nullptr, uint16_t size = 0);

  struct bt_le_oob oob_local;
  struct bt_le_oob oob_remote;

  char localOobMessage[oobMessageLen];

  OobTransport *transport;
};

class OobTransport {
 public:
  virtual void Init(struct k_msgq * msgq) = 0;
  virtual void Deinit() = 0;
  virtual void Send(char *buf) = 0;

 protected:
  uint16_t rx_buf_pos; 
  char rx_buf[OobExchangeManager::oobMessageLen];
  struct k_msgq * rx_msgq;
};
