#pragma once

#include <bluetooth/scan.h>
#include <lib/core/CHIPError.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>

class MatterDevice;

class BLEConnectivityManager {
 public:
  static constexpr uint16_t kScanTimeoutMs = 10000;

  using ScanCallback = void (*)(void *ctx, bool connected, struct bt_conn *conn,
                                struct bt_uuid *serviceUuid);

  struct DeviceFilter {
    enum { FILTER_TYPE_NAME, FILTER_TYPE_UUID, FILTER_TYPE_ADDR } type;
    union {
      char name[16];
      struct bt_uuid *serviceUuid;
      bt_addr_le_t deviceAddress;
    } filter;
  };

  CHIP_ERROR Init();
  CHIP_ERROR Scan(void *ctx, ScanCallback cb, DeviceFilter filter,
                  uint32_t scanTimeoutMs = kScanTimeoutMs);
  CHIP_ERROR StopScan();
  int Connect(const bt_addr_le_t *addr, const bt_le_conn_param *connParams = BT_LE_CONN_PARAM_DEFAULT);
  int ConnectFirstBondedDevice(void *ctx, ScanCallback cb, DeviceFilter filter);
  void ExchangeOob();
  
  void StopTimer();

  static BLEConnectivityManager &Instance() {
    static BLEConnectivityManager sInstance;
    return sInstance;
  }

  static void SecurityChangedHandler(struct bt_conn *conn, bt_security_t level,
                                     enum bt_security_err err);
  static void IdentityResolvedHandler(struct bt_conn *conn, const bt_addr_le_t *rpa,
                                      const bt_addr_le_t *identity);
  static void DisconnectionHandler(bt_conn *conn, uint8_t reason);
  static void ConnectionHandler(bt_conn *conn, uint8_t conn_err);
  static void ScanResultCallback(bt_scan_device_info *device_info,
                                 bt_scan_filter_match *filter_match, bool connectable);
  static void Connecting(struct bt_scan_device_info *device_info, struct bt_conn *conn);

  static void ScanTimeoutCallback(k_timer *timer);

  struct connectionInfo {
    struct bt_conn *conn;
    bt_addr_le_t addr = bt_addr_le_none;
    struct bt_uuid *serviceUuid;     
    void *ctx;
    ScanCallback cb;
  };

  struct scanInfo {
    bool scanning;
    struct bt_uuid *serviceUuid;
    void *ctx;
    ScanCallback cb;
  };  

  // The purpose of this struct is 
  //   - to have the context, callback and service uuid during the (Dis)connected callbacks.
  //   - to react only to (dis)connects that were initiated by the bridge manager.
  struct connectionInfo myConnections[CONFIG_BT_MAX_CONN];
  struct scanInfo scanState;

 private:
  CHIP_ERROR PrepareFilterForUuid(bt_uuid *serviceUuid);

  const struct bt_conn_le_create_param *create_param = BT_CONN_LE_CREATE_CONN;

  k_timer mScanTimer;
  bt_uuid mServiceUuid;
};
