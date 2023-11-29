#include "oob_exchange_manager.h"

#include <zephyr/bluetooth/addr.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
// https://stackoverflow.com/questions/23462307/enumerations-within-a-struct-c-vs-c
enum {
  /** Local OOB data requested */
  BT_CONN_OOB_LOCAL_ONLY,

  /** Remote OOB data requested */
  BT_CONN_OOB_REMOTE_ONLY,

  /** Both local and remote OOB data requested */
  BT_CONN_OOB_BOTH_PEERS,

  /** No OOB data requested */
  BT_CONN_OOB_NO_DATA,
} oob_config;
#pragma GCC diagnostic pop

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wenum-compare"
static const char *oob_config_str(int oob_config) {
  switch (oob_config) {
    case BT_CONN_OOB_LOCAL_ONLY:
      return "Local";
    case BT_CONN_OOB_REMOTE_ONLY:
      return "Remote";
    case BT_CONN_OOB_BOTH_PEERS:
      return "Local and Remote";
    case BT_CONN_OOB_NO_DATA:
    default:
      return "no";
  }
}

void OobExchangeManager::AuthPairingOobDataRequestEntry(struct bt_conn *conn,
                                                        struct bt_conn_oob_info *oob_info) {
  Instance().AuthPairingOobDataRequest(conn, oob_info);
}

void OobExchangeManager::AuthPairingOobDataRequest(struct bt_conn *conn,
                                                   struct bt_conn_oob_info *oob_info) {
  char addr[BT_ADDR_LE_STR_LEN];
  struct bt_conn_info info;
  int err;

  err = bt_conn_get_info(conn, &info);
  if (err) {
    return;
  }

  if (oob_info->type == bt_conn_oob_info::BT_CONN_OOB_LE_SC) {
    struct bt_le_oob_sc_data *oobd_local =
        oob_info->lesc.oob_config != BT_CONN_OOB_REMOTE_ONLY ? &oob_local.le_sc_data : NULL;
    struct bt_le_oob_sc_data *oobd_remote =
        oob_info->lesc.oob_config != BT_CONN_OOB_LOCAL_ONLY ? &oob_remote.le_sc_data : NULL;

    if (oobd_remote && !bt_addr_le_eq(info.le.remote, &oob_remote.addr)) {
      bt_addr_le_to_str(info.le.remote, addr, sizeof(addr));
      LOG_INF("No OOB data available for remote %s", addr);
      bt_conn_auth_cancel(conn);
      return;
    }

    if (oobd_local && !bt_addr_le_eq(info.le.local, &oob_local.addr)) {
      bt_addr_le_to_str(info.le.local, addr, sizeof(addr));
      LOG_INF("No OOB data available for local %s", addr);
      bt_conn_auth_cancel(conn);
      return;
    }

    bt_le_oob_set_sc_data(conn, oobd_local, oobd_remote);

    bt_addr_le_to_str(info.le.dst, addr, sizeof(addr));
    LOG_INF("Set %s OOB SC data for %s, ", oob_config_str(oob_info->lesc.oob_config), addr);
    return;
  }

  bt_addr_le_to_str(info.le.dst, addr, sizeof(addr));
  LOG_ERR("Legacy OOB not supported! TK requested from remote %s", addr);
}
#pragma GCC diagnostic pop

void OobExchangeManager::PrintLeOob(struct bt_le_oob *oob, char *buffer, uint16_t size) {
  char addr[BT_ADDR_LE_STR_LEN];
  char c[keyStrLen];
  char r[keyStrLen];

  bt_addr_le_to_str(&oob->addr, addr, sizeof(addr));

  bin2hex(oob->le_sc_data.c, sizeof(oob->le_sc_data.c), c, sizeof(c));
  bin2hex(oob->le_sc_data.r, sizeof(oob->le_sc_data.r), r, sizeof(r));

  LOG_INF("OOB data:");
  LOG_INF("%-29s %-32s %-32s", "addr", "random", "confirm");
  LOG_INF("%29s %32s %32s", addr, r, c);
  if (buffer && size >= oobMessageLen) {
    snprintf(buffer, oobMessageLen, "%29s %32s %32s\n", addr, r, c);
  }
}

void OobExchangeManager::SetRemoteOob(const char *addr, const char *type, const char *r,
                                      const char *c) {
  bt_addr_le_t bt_addr;

  int err = bt_addr_le_from_str(addr, type, &bt_addr);
  if (err) {
    LOG_ERR("Invalid peer address (err %d)", err);
    return;
  }

  bt_addr_le_copy(&oob_remote.addr, &bt_addr);
  hex2bin(r, strlen(r), oob_remote.le_sc_data.r, sizeof(oob_remote.le_sc_data.r));
  hex2bin(c, strlen(c), oob_remote.le_sc_data.c, sizeof(oob_remote.le_sc_data.c));
  bt_le_oob_set_sc_flag(true);

  PrintLeOob(&oob_remote);
}

void OobExchangeManager::GetLocalOob(char *buffer, uint16_t size) {
  int err = bt_le_oob_get_local(BT_ID_DEFAULT, &oob_local);
  if (err) {
    LOG_ERR("Retrieving OOB data failed (err %d)", err);
    return;
  }

  PrintLeOob(&oob_local, buffer, size);
}

K_MSGQ_DEFINE(transportMsgq, OobExchangeManager::oobMessageLen, 2, 4);

void OobExchangeManager::ExchangeOob(OobTransport *transport) {
  transport->Init(&transportMsgq);
  GetLocalOob(localOobMessage, sizeof(localOobMessage));
  transport->Send(localOobMessage);
  LOG_INF("Send to UART: %s", localOobMessage);

  // Only the device that requests pairing needs the other sides' OOB data
  // Below is not necessary.

  char tx_buf[oobMessageLen];
  while (k_msgq_get(&transportMsgq, &tx_buf, K_MSEC(100)) == 0) {
    LOG_INF("Received remote oob data: %s", tx_buf);

    char *addr = strtok(tx_buf, " ");
    if (addr == NULL) continue;
    char *type = strtok(NULL, " ");
    if (type == NULL) continue;
    char *r = strtok(NULL, " ");
    if (r == NULL) continue;
    char *c = strtok(NULL, " ");
    if (c == NULL) continue;

    SetRemoteOob(addr, type, r, c);

    /* Does not fit into flash by over 100KB!

    #include <iterator>
    #include <sstream>
    #include <string>
    #include <vector>

    // https://stackoverflow.com/questions/236129/how-do-i-iterate-over-the-words-of-a-string
    std::istringstream iss(tx_buf);
    std::vector<std::string> tokens{std::istream_iterator<std::string>{iss},
    std::istream_iterator<std::string>{}}; if (tokens.size() == 4) {
      SetRemoteOob(tokens.at(0).c_str(), tokens.at(1).c_str(), tokens.at(2).c_str(),
    tokens.at(3).c_str());
    }
    */
  }
  transport->Deinit();
}
