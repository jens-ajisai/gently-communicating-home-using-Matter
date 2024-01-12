#include "ble_connectivity_manager.h"

#include <bluetooth/gatt_dm.h>
#include <bluetooth/scan.h>
#include <lib/core/CHIPError.h>
#include <platform/CHIPDeviceLayer.h>
#include <zephyr/bluetooth/addr.h>

#include "oob_exchange_manager.h"

extern "C" {
#include <zephyr/bluetooth/conn.h>
}
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

#define CONNECT_IF_MATCH false

static void pairing_complete(struct bt_conn *conn, bool bonded) {
  char addr[BT_ADDR_LE_STR_LEN];
  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
  LOG_INF("Pairing completed: %s, bonded: %d\n", addr, bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason) {
  char addr[BT_ADDR_LE_STR_LEN];
  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
  LOG_INF("Pairing failed conn: %s, reason %d\n", addr, reason);
}

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {.pairing_complete = pairing_complete,
                                                               .pairing_failed = pairing_failed};

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey) {
  char addr[BT_ADDR_LE_STR_LEN];
  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
  LOG_INF("Passkey for %s: %06u. Please enter via shell command.", addr, passkey);
}

/*
static void auth_passkey_entry(struct bt_conn *conn)
{
        char addr[BT_ADDR_LE_STR_LEN];
        bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
        LOG_INF("Enter passkey for %s", addr);
}
*/

static void auth_passkey_confirm(struct bt_conn *conn, unsigned int passkey) {
  char addr[BT_ADDR_LE_STR_LEN];
  char passkey_str[7];

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
  snprintk(passkey_str, 7, "%06u", passkey);
  LOG_INF("Confirm passkey for %s: %s", addr, passkey_str);

  bt_conn_auth_passkey_confirm(conn);
}

static void auth_cancel(struct bt_conn *conn) {
  char addr[BT_ADDR_LE_STR_LEN];
  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
  LOG_INF("Pairing cancelled: %s", addr);
}

static void auth_pairing_confirm(struct bt_conn *conn) {
  char addr[BT_ADDR_LE_STR_LEN];
  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
  LOG_INF("Confirm pairing for %s", addr);

  bt_conn_auth_pairing_confirm(conn);
}

#if defined(CONFIG_BT_SMP_APP_PAIRING_ACCEPT)
enum bt_security_err pairing_accept(struct bt_conn *conn,
                                    const struct bt_conn_pairing_feat *const feat) {
  LOG_INF(
      "Remote pairing features: "
      "IO: 0x%02x, OOB: %d, AUTH: 0x%02x, Key: %d, "
      "Init Kdist: 0x%02x, Resp Kdist: 0x%02x",
      feat->io_capability, feat->oob_data_flag, feat->auth_req, feat->max_enc_key_size,
      feat->init_key_dist, feat->resp_key_dist);

  return BT_SECURITY_ERR_SUCCESS;
}
#endif /* CONFIG_BT_SMP_APP_PAIRING_ACCEPT */

static struct bt_conn_auth_cb conn_auth_callbacks = {
#if defined(CONFIG_BT_SMP_APP_PAIRING_ACCEPT)
    .pairing_accept = pairing_accept,
#endif
    .passkey_display = auth_passkey_display,
    .passkey_entry = NULL,  // auth_passkey_entry,
    .passkey_confirm = auth_passkey_confirm,
    .oob_data_request = OobExchangeManager::AuthPairingOobDataRequestEntry,
    .cancel = auth_cancel,
    .pairing_confirm = auth_pairing_confirm,
};

BT_SCAN_CB_INIT(scan_cb, BLEConnectivityManager::ScanResultCallback, NULL, NULL,
                BLEConnectivityManager::Connecting);

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = BLEConnectivityManager::ConnectionHandler,
    .disconnected = BLEConnectivityManager::DisconnectionHandler,
    .identity_resolved = BLEConnectivityManager::IdentityResolvedHandler,
    .security_changed = BLEConnectivityManager::SecurityChangedHandler};

void BLEConnectivityManager::SecurityChangedHandler(struct bt_conn *conn, bt_security_t level,
                                                    enum bt_security_err err) {
  char addr[BT_ADDR_LE_STR_LEN];
  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  struct bt_conn_info info;
  bt_conn_get_info(conn, &info);

  if (!err) {
    LOG_INF("Security changed: %s level %u", addr, level);
    LOG_INF("  ... Security level: %d, flag: %d", info.security.level, info.security.flags);
  } else {
    LOG_INF("Security failed: %s level %u err %d", addr, level, err);
  }
}

void BLEConnectivityManager::IdentityResolvedHandler(struct bt_conn *conn, const bt_addr_le_t *rpa,
                                                     const bt_addr_le_t *identity) {
  char addr_identity[BT_ADDR_LE_STR_LEN];
  char addr_rpa[BT_ADDR_LE_STR_LEN];

  bt_addr_le_to_str(identity, addr_identity, sizeof(addr_identity));
  bt_addr_le_to_str(rpa, addr_rpa, sizeof(addr_rpa));

  LOG_INF("BT Identity resolved %s -> %s.", addr_rpa, addr_identity);
}

void BLEConnectivityManager::DisconnectionHandler(bt_conn *conn, uint8_t reason) {
  char addrStr[BT_ADDR_LE_STR_LEN];
  bt_addr_le_to_str(bt_conn_get_dst(conn), addrStr, sizeof(addrStr));
  LOG_INF("Disconnected: %s (reason %u)", addrStr, reason);

  for (size_t i = 0; i < ARRAY_SIZE(Instance().myConnections); i++) {
    if (Instance().myConnections[i].conn == conn) {
      Instance().myConnections[i].cb(Instance().myConnections[i].ctx, false,
                                     Instance().myConnections[i].conn,
                                     Instance().myConnections[i].serviceUuid);
      bt_addr_le_copy(&Instance().myConnections[i].addr, &bt_addr_le_none);

      // We may not unref the connection managed by Matter. It will crash!!!
      // unref pair to implicit ref in the connect function
      bt_conn_unref(conn);
      break;
    }
  }
}

void BLEConnectivityManager::ConnectionHandler(bt_conn *conn, uint8_t conn_err) {
  char addrStr[BT_ADDR_LE_STR_LEN];
  bt_addr_le_to_str(bt_conn_get_dst(conn), addrStr, sizeof(addrStr));
  LOG_INF("Connected: %s", addrStr);

  struct bt_conn_info info;
  bt_conn_get_info(conn, &info);
  LOG_INF("  ... Security level: %d, flag: %d", info.security.level, info.security.flags);

  for (size_t i = 0; i < ARRAY_SIZE(Instance().myConnections); i++) {
    if (Instance().myConnections[i].conn == conn) {
      Instance().myConnections[i].cb(Instance().myConnections[i].ctx, true, conn,
                                     Instance().myConnections[i].serviceUuid);
      break;
    }
  }
}

// This is only called when connect_if_match == true
void BLEConnectivityManager::Connecting(struct bt_scan_device_info *device_info,
                                        struct bt_conn *conn) {
  char addr[BT_ADDR_LE_STR_LEN];
  bt_addr_le_to_str(device_info->recv_info->addr, addr, sizeof(addr));
  LOG_INF("Connecting: %s", addr);
}

void BLEConnectivityManager::ScanResultCallback(bt_scan_device_info *device_info,
                                                bt_scan_filter_match *filter_match,
                                                bool connectable) {
  if (!filter_match) {
    LOG_ERR("Filter match error");
    return;
  }

  char addr[BT_ADDR_LE_STR_LEN];
  bt_addr_le_to_str(device_info->recv_info->addr, addr, sizeof(addr));
  LOG_INF("Scan result: %s", addr);

  if (filter_match->uuid.match) {
    if (bt_uuid_cmp(Instance().scanState.serviceUuid, filter_match->uuid.uuid[0]) == 0) {
      if (!CONNECT_IF_MATCH) {
        for (size_t i = 0; i < ARRAY_SIZE(Instance().myConnections); i++) {
          if (bt_addr_le_eq(&Instance().myConnections[i].addr, &bt_addr_le_none)) {
            bt_addr_le_copy(&Instance().myConnections[i].addr, device_info->recv_info->addr);
            Instance().myConnections[i].serviceUuid = Instance().scanState.serviceUuid;
            Instance().myConnections[i].ctx = Instance().scanState.ctx;
            Instance().myConnections[i].cb = Instance().scanState.cb;
            break;
          }
        }
        Instance().StopScan();
        Instance().Connect(device_info->recv_info->addr, device_info->conn_param);
      }
    }
  }
}

CHIP_ERROR BLEConnectivityManager::PrepareFilterForUuid(bt_uuid *serviceUuid) {
  int err;

  bt_scan_filter_disable();
  bt_scan_filter_remove_all();

  err = bt_scan_filter_add(BT_SCAN_FILTER_TYPE_UUID, serviceUuid);

  if (err) {
    LOG_ERR("Failed to set scanning filter");
    return chip::System::MapErrorZephyr(err);
  }

  err = bt_scan_filter_enable(BT_SCAN_UUID_FILTER, false);
  if (err) {
    LOG_ERR("Filters cannot be turned on");
    return chip::System::MapErrorZephyr(err);
  }

  return CHIP_NO_ERROR;
}

CHIP_ERROR BLEConnectivityManager::PrepareFilterForAddr(bt_addr_le_t *addr) {
  int err;

  bt_scan_filter_disable();
  bt_scan_filter_remove_all();

  err = bt_scan_filter_add(BT_SCAN_FILTER_TYPE_ADDR, addr);

  if (err) {
    LOG_ERR("Failed to set scanning filter");
    return chip::System::MapErrorZephyr(err);
  }

  err = bt_scan_filter_enable(BT_SCAN_ADDR_FILTER, false);
  if (err) {
    LOG_ERR("Filters cannot be turned on");
    return chip::System::MapErrorZephyr(err);
  }

  return CHIP_NO_ERROR;
}


CHIP_ERROR BLEConnectivityManager::Init() {
  LOG_INF("BLEConnectivityManager::Init()");

  k_timer_init(&mScanTimer, BLEConnectivityManager::ScanTimeoutCallback, nullptr);
  k_timer_user_data_set(&mScanTimer, this);

  bt_scan_init_param scan_init = {
      .connect_if_match = CONNECT_IF_MATCH,
  };

  bt_scan_init(&scan_init);
  bt_scan_cb_register(&scan_cb);
  bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
  bt_conn_auth_cb_register(&conn_auth_callbacks);

  return CHIP_NO_ERROR;
}

CHIP_ERROR BLEConnectivityManager::Scan(void *ctx, ScanCallback cb, DeviceFilter filter,
                                        uint32_t scanTimeoutMs) {
  CHIP_ERROR ret = CHIP_NO_ERROR;
  char str[BT_UUID_STR_LEN];

  if (scanState.scanning) {
    LOG_WRN("Scan is already in progress. Restart scanning");
    Instance().StopScan();
  }

  scanState.scanning = true;
  scanState.ctx = ctx;
  scanState.cb = cb;

  switch (filter.type) {
    case DeviceFilter::FILTER_TYPE_UUID:
      ret = PrepareFilterForUuid(filter.filter.serviceUuid);
      bt_uuid_to_str(filter.filter.serviceUuid, str, sizeof(str));
      LOG_INF("Scan for uuid %s", str);
      scanState.serviceUuid = filter.filter.serviceUuid;
      break;
    case DeviceFilter::FILTER_TYPE_ADDR:
      ret = PrepareFilterForAddr(&filter.filter.deviceAddress);
      bt_addr_le_to_str(&filter.filter.deviceAddress, str, sizeof(str));
      LOG_INF("Scan for addr %s", str);
      scanState.serviceUuid = filter.filter.serviceUuid;
      break;
    default:
      LOG_ERR("Not implemented. Only FILTER_TYPE_UUID supported");
      return CHIP_ERROR_NOT_IMPLEMENTED;
      break;
  }

  VerifyOrReturnValue(ret == CHIP_NO_ERROR, ret,
                      LOG_ERR("Scan filter preparation not successful."));

  ret = chip::System::MapErrorZephyr(bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE));
  VerifyOrReturnValue(ret == CHIP_NO_ERROR, ret, LOG_ERR("Scan start not successful."));

  k_timer_start(&mScanTimer, K_MSEC(scanTimeoutMs), K_NO_WAIT);
  return ret;
}

CHIP_ERROR BLEConnectivityManager::StopScan() {
  LOG_INF("Stop scanning");

  StopTimer();
  scanState.scanning = false;
  scanState.serviceUuid = nullptr;
  scanState.ctx = nullptr;
  scanState.cb = nullptr;

  int err = bt_scan_stop();
  if (err) {
    LOG_ERR("Scanning failed to stop (err %d)", err);
    return chip::System::MapErrorZephyr(err);
  }

  return CHIP_NO_ERROR;
}

void BLEConnectivityManager::ScanTimeoutCallback(k_timer *timer) {
  LOG_INF("Scan timeout");
  Instance().scanState.cb(Instance().scanState.ctx, false, nullptr,
                          Instance().scanState.serviceUuid);
  Instance().StopScan();
}

void BLEConnectivityManager::StopTimer() { k_timer_stop(&mScanTimer); }

int BLEConnectivityManager::ConnectFirstBondedDevice(void *ctx, ScanCallback cb,
                                                     DeviceFilter filter) {
  LOG_INF("BLEConnectivityManager::ConnectFirstBondedDevice");
  bt_addr_le_t addr = bt_addr_le_none;

  if (bt_addr_le_eq(&addr, &bt_addr_le_none)) {
    LOG_INF("Iterate bonded devices.");
    bt_foreach_bond(
        BT_ID_DEFAULT,
        [](const struct bt_bond_info *info, void *user_data) {
          memcpy(user_data, &info->addr, sizeof(bt_addr_le_t));
        },
        &addr);
  } else {
    LOG_INF("Use address of OOB data.");
  }

  if (!bt_addr_le_eq(&addr, &bt_addr_le_none)) {
    for (size_t i = 0; i < ARRAY_SIZE(myConnections); i++) {
      if (bt_addr_le_eq(&myConnections[i].addr, &bt_addr_le_none)) {
        bt_addr_le_copy(&myConnections[i].addr, &addr);
        myConnections[i].ctx = ctx;
        myConnections[i].cb = cb;

        switch (filter.type) {
          case DeviceFilter::FILTER_TYPE_UUID:
            myConnections[i].serviceUuid = filter.filter.serviceUuid;
            break;
          default:
            LOG_ERR("Not implemented. Only FILTER_TYPE_UUID supported");
            return 0;
        }

        Instance().Connect(&myConnections[i].addr);
        LOG_INF("  ... connect to bonded device");
        break;
      }
    }
    return true;
  }

  return false;
}

int BLEConnectivityManager::Connect(const bt_addr_le_t *addr, const bt_le_conn_param *connParams) {
  char addrS[BT_ADDR_LE_STR_LEN];
  bt_addr_le_to_str(addr, addrS, sizeof(addrS));
  LOG_INF("BLEConnectivityManager::Connect to addr %s", addrS);

  bt_conn *conn;
  int err = bt_conn_le_create(addr, create_param, connParams, &conn);

  if (err) {
    LOG_ERR("Creating connection failed (err %d)", err);
  } else {
    for (size_t i = 0; i < ARRAY_SIZE(myConnections); i++) {
      if (bt_addr_le_eq(&myConnections[i].addr, addr)) {
        myConnections[i].conn = conn;
        break;
      }
    }
  }

  return err;
}
