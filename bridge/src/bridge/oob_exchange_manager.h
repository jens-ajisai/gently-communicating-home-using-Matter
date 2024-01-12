#pragma once

#include <bluetooth/scan.h>
#include <lib/core/CHIPError.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>

class OobExchangeManager {
 public:
  static constexpr uint16_t keyStrLen = 33;
  static constexpr uint16_t oobMessageLen = (BT_ADDR_LE_STR_LEN + keyStrLen + keyStrLen + 1);

  using SendOobData = void (*)(char *buf);     

  static OobExchangeManager &Instance() {
    static OobExchangeManager sInstance;
    return sInstance;
  }

  static void AuthPairingOobDataRequestEntry(struct bt_conn *conn, struct bt_conn_oob_info *oob_info);
  void AuthPairingOobDataRequest(struct bt_conn *conn, struct bt_conn_oob_info *oob_info);

  void ExchangeOob(char* receivedOobData, SendOobData sendFunction);

  // Public for shell access
  void SetRemoteOob(const char *addr, const char *type, const char *r, const char *c);
  void GetLocalOob(char *buffer = nullptr, uint16_t size = 0);

  bt_addr_le_t GetRemoteAddr() { return bt_addr_remote; }

 private:
  void PrintLeOob(struct bt_le_oob *oob, char *buffer = nullptr, uint16_t size = 0);

  struct bt_le_oob oob_local;
  struct bt_le_oob oob_remote;
  bt_addr_le_t bt_addr_remote = bt_addr_le_none;  

  char localOobMessage[oobMessageLen];
};
