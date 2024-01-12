#pragma once

#include <bluetooth/gatt_dm.h>
#include <lib/support/CHIPMem.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>

#include <map>
#include <utility>

#define GATT_READ_BUF_SIZE 24

// Cyclic dependencies -> Forward declaration.
class MatterDevice;

class BleDevice {
 public:
  using DiscoveryCallback = void (*)(void *ctx);
  using SubscriptionCallback = void (*)(void *ctx, struct bt_uuid *charUuid, const void *data,
                                        uint16_t length);

  struct DiscoveryContext {
    void *dev;
    DiscoveryCallback cb;
  };

  BleDevice(bt_conn *conn) {
    mConn = conn;
    bt_conn_ref(mConn);
    addInstance(mConn, this);
  }
  ~BleDevice() {
    printk("~BleDevice");
    bt_conn_unref(mConn);
    printk("bt_conn_unref OK");

    for (const auto &[key, value] : mHandleToUuid) {
      if (value) {
        printk("Delete value");
        chip::Platform::Delete(value);
      }
    }

    for (const auto &[key, value] : mSubCbs) {
      if (key) {
        printk("Delete key");
        chip::Platform::Delete(key);
      }
    }
  }

  static std::map<bt_conn *, BleDevice *> instances;
  static BleDevice *Instance(bt_conn *conn) { return instances[conn]; }

  void Discover(void *ctx, DiscoveryCallback cb, struct bt_uuid *serviceUuid);
  void Unsubscribe(struct bt_uuid *charUuid);
  void Subscribe(void *ctx, SubscriptionCallback cb, bt_uuid *charUuid);
  bool Read(struct bt_uuid *uuid, uint8_t *buffer, uint16_t maxReadLength);

  void DiscoveryNotFound(bt_conn *conn, void *context);
  void DiscoveryError(bt_conn *conn, int err, void *context);
  void DiscoveryCompletedHandler(bt_gatt_dm *dm, void *context);
  void SubscriptionHandler(bt_conn *conn, bt_gatt_subscribe_params *params, const void *data,
                           uint16_t length);
  void GATTReadCallback(bt_conn *conn, uint8_t att_err, bt_gatt_read_params *params,
                        const void *data, uint16_t read_len);

  void Disconnect();

 private:
  void addInstance(bt_conn *conn, BleDevice *dev) { instances[conn] = dev; }
  void removeInstance(bt_conn *conn) { instances.erase(conn); }

  bool CheckSubscriptionParameters(bt_gatt_subscribe_params *params);

  uint16_t findHandleByUuid(struct bt_uuid *uuid);
  uint16_t findNextCccdHandleByUuid(uint16_t attrHandle);

  bt_conn *mConn;
  std::map<uint16_t, struct bt_uuid *> mHandleToUuid;
  std::map<bt_gatt_subscribe_params *, std::pair<void *, SubscriptionCallback>> mSubCbs;
  struct DiscoveryContext mDisCb = {NULL, NULL};

  uint8_t mGattReadBuffer[GATT_READ_BUF_SIZE];
  uint16_t mGattReadBufferSize = GATT_READ_BUF_SIZE;
  uint16_t mGattReadSize = 0;
  struct k_poll_signal mGattReadSignal;
  struct k_poll_event mGattWaitEvents[1];
};
