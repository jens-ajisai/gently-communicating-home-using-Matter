#include "ble_device.h"

#include <bluetooth/gatt_dm.h>
#include <lib/support/CHIPMem.h>
#include <platform/PlatformManager.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <map>
#include <utility>

#include "matter_device.h"
LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

std::map<bt_conn *, BleDevice *> BleDevice::instances;

static void DiscoveryNotFoundEntry(bt_conn *conn, void *context) {
  BleDevice *dev = BleDevice::Instance(conn);
  dev->DiscoveryNotFound(conn, context);
}

static void DiscoveryErrorEntry(bt_conn *conn, int err, void *context) {
  BleDevice *dev = BleDevice::Instance(conn);
  dev->DiscoveryError(conn, err, context);
}

static void DiscoveryCompletedHandlerEntry(bt_gatt_dm *dm, void *context) {
  BleDevice *dev = BleDevice::Instance(bt_gatt_dm_conn_get(dm));
  dev->DiscoveryCompletedHandler(dm, context);
}

static uint8_t SubscriptionHandlerEntry(bt_conn *conn, bt_gatt_subscribe_params *params,
                                        const void *data, uint16_t length) {
  BleDevice *dev = BleDevice::Instance(conn);
  dev->SubscriptionHandler(conn, params, data, length);
  return BT_GATT_ITER_CONTINUE;
}

static uint8_t GATTReadCallbackEntry(bt_conn *conn, uint8_t att_err, bt_gatt_read_params *params,
                                     const void *data, uint16_t read_len) {
  BleDevice *dev = BleDevice::Instance(conn);
  dev->GATTReadCallback(conn, att_err, params, data, read_len);
  return BT_GATT_ITER_STOP;
}

static const struct bt_gatt_dm_cb discovery_cb = {.completed = DiscoveryCompletedHandlerEntry,
                                                  .service_not_found = DiscoveryNotFoundEntry,
                                                  .error_found = DiscoveryErrorEntry};

uint16_t BleDevice::findHandleByUuid(struct bt_uuid *uuid) {
  auto findResult = std::find_if(std::begin(mHandleToUuid), std::end(mHandleToUuid),
                                 [&](const std::pair<uint16_t, struct bt_uuid *> &pair) {
                                   return bt_uuid_cmp(uuid, pair.second) == 0;
                                 });

  uint16_t foundKey = 0;
  if (findResult != std::end(mHandleToUuid)) {
    foundKey = findResult->first;
  }

  char str[BT_UUID_STR_LEN];
  bt_uuid_to_str(uuid, str, sizeof(str));
  LOG_INF("Found handle %d for uuid %s", foundKey, str);
  return foundKey;
}

uint16_t BleDevice::findNextCccdHandleByUuid(uint16_t attrHandle) {
  struct bt_uuid_16 cccd = BT_UUID_INIT_16(BT_UUID_GATT_CCC_VAL);
  auto findResult = std::find_if(mHandleToUuid.lower_bound(attrHandle), std::end(mHandleToUuid),
                                 [&](const std::pair<uint16_t, struct bt_uuid *> &pair) {
                                   return bt_uuid_cmp((bt_uuid *)&cccd, pair.second) == 0;
                                 });

  uint16_t foundKey = 0;
  if (findResult != std::end(mHandleToUuid)) {
    foundKey = findResult->first;
  }

  LOG_INF("Found cccd handle %d for attr handle %d", foundKey, attrHandle);
  return foundKey;
}

void BleDevice::DiscoveryCompletedHandler(bt_gatt_dm *dm, void *context) {
  LOG_INF("The GATT discovery completed");
  bt_gatt_dm_data_print(dm);

  BleDevice *dev = BleDevice::Instance(bt_gatt_dm_conn_get(dm));

  const struct bt_gatt_dm_attr *attr = NULL;
  while (NULL != (attr = bt_gatt_dm_attr_next(dm, attr))) {
    if (attr->uuid->type == BT_UUID_TYPE_16) {
      auto *uuid = chip::Platform::New<bt_uuid_16>();
      uuid = BT_UUID_16(attr->uuid);
      mHandleToUuid[attr->handle] = reinterpret_cast<bt_uuid *>(uuid);
    } else if (attr->uuid->type == BT_UUID_TYPE_32) {
      auto *uuid = chip::Platform::New<bt_uuid_32>();
      uuid = BT_UUID_32(attr->uuid);
      mHandleToUuid[attr->handle] = reinterpret_cast<bt_uuid *>(uuid);
    } else if (attr->uuid->type == BT_UUID_TYPE_128) {
      auto *uuid = chip::Platform::New<bt_uuid_128>();
      uuid = BT_UUID_128(attr->uuid);
      mHandleToUuid[attr->handle] = reinterpret_cast<bt_uuid *>(uuid);
    }
  }

  bt_gatt_dm_data_release(dm);

  chip::DeviceLayer::PlatformMgr().ScheduleWork(
      [](intptr_t context) {
        BleDevice *dev = reinterpret_cast<BleDevice *>(context);
        dev->mDisCb.cb(dev->mDisCb.dev);
        dev->mDisCb.dev = NULL;
        dev->mDisCb.cb = NULL;
      },
      reinterpret_cast<intptr_t>(dev));
}

void BleDevice::DiscoveryNotFound(bt_conn *conn, void *context) {
  mDisCb.dev = NULL;
  mDisCb.cb = NULL;
  LOG_ERR("GATT service could not be found during the discovery");
}

void BleDevice::DiscoveryError(bt_conn *conn, int err, void *context) {
  mDisCb.dev = NULL;
  mDisCb.cb = NULL;
  LOG_ERR("The GATT discovery procedure failed with %d", err);
}

void BleDevice::Discover(void *ctx, DiscoveryCallback cb, struct bt_uuid *serviceUuid) {
  if (mDisCb.dev) {
    LOG_ERR("Discovery already ongoing.");
  } else {
    char str[BT_UUID_STR_LEN];
    bt_uuid_to_str(serviceUuid, str, sizeof(str));
    LOG_INF("Start discovery for uuid %s", str);

    mDisCb.dev = ctx;
    mDisCb.cb = cb;

    int err = bt_gatt_dm_start(mConn, serviceUuid, &discovery_cb, (void *)this);
    if (err) {
      LOG_ERR(
          "Could not start the discovery procedure, error "
          "code: %d",
          err);
    }
  }
}

void BleDevice::SubscriptionHandler(bt_conn *conn, bt_gatt_subscribe_params *params,
                                    const void *data, uint16_t length) {
  // subscriptions of bonded devices survive a disconnect
  if(mHandleToUuid[params->value_handle] != nullptr) {
    char str[BT_UUID_STR_LEN];
    bt_uuid_to_str(mHandleToUuid[params->value_handle], str, sizeof(str));
    LOG_INF("Subscription handler for uuid %s: ", str);
    LOG_HEXDUMP_INF(data, length, "Subscription data");

    auto subCb = mSubCbs[params];
    if (subCb.second != nullptr)
      subCb.second(subCb.first, mHandleToUuid[params->value_handle], data, length);
  }
}

bool BleDevice::CheckSubscriptionParameters(bt_gatt_subscribe_params *params) {
  LOG_INF("CheckSubscriptionParameters ...");
  /* If any of these is not met, the bt_gatt_subscribe() generates an assert at runtime */
  VerifyOrReturnValue(params && params->notify, false, LOG_ERR("No param or no notify callback"));
  VerifyOrReturnValue(params->value, false, LOG_ERR("Invalid char handle"));
  VerifyOrReturnValue(params->ccc_handle, false, LOG_ERR("Invalid ccc handle"));

  LOG_INF("... OK");
  return true;
}

void BleDevice::Subscribe(void *ctx, SubscriptionCallback cb, bt_uuid *charUuid) {
  char str[BT_UUID_STR_LEN];
  bt_uuid_to_str(charUuid, str, sizeof(str));
  LOG_INF("Subscribe uuid %s for ctx %p", str, ctx);

  VerifyOrReturn(mConn, LOG_ERR("Invalid connection object"));

  auto *params = chip::Platform::New<bt_gatt_subscribe_params>();
  params->value_handle = findHandleByUuid(charUuid);
  params->ccc_handle = findNextCccdHandleByUuid(params->value_handle);
  params->value = BT_GATT_CCC_NOTIFY;
  params->notify = SubscriptionHandlerEntry;

  mSubCbs[params] = std::pair<void *, SubscriptionCallback>(ctx, cb);

  if (CheckSubscriptionParameters(params)) {
    int err = bt_gatt_subscribe(mConn, params);
    if (err) {
      LOG_ERR("Subscribe to posture characteristic failed with error %d", err);
    } else {
      LOG_INF("Subscribe OK");
    }
  } else {
    LOG_ERR("Subscription parameter verification failed. value_handle=%d,  ccc_handle=%d",
            params->value_handle, params->ccc_handle);
  }
}

void BleDevice::Unsubscribe(struct bt_uuid *charUuid) {
  char str[BT_UUID_STR_LEN];
  bt_uuid_to_str(charUuid, str, sizeof(str));
  LOG_INF("Unsubscribe uuid %s", str);
  VerifyOrReturn(mConn, LOG_ERR("Invalid connection object"));

  for (const auto &[key, value] : mSubCbs) {
    if (key->value_handle == findHandleByUuid(charUuid)) {
      int err = bt_gatt_unsubscribe(mConn, key);
      if (err) {
        LOG_INF("Cannot unsubscribe from posture characteristic (error %d)", err);
      }
      chip::Platform::Delete(key);
      return;
    }
  }
}

void BleDevice::GATTReadCallback(bt_conn *conn, uint8_t att_err, bt_gatt_read_params *params,
                                 const void *data, uint16_t read_len) {
  LOG_INF("GATT read handler for handle %d (size %d, result %d): ", params->single.handle, read_len,
          att_err);
  LOG_HEXDUMP_INF(data, read_len, "  ... data ");

  if (!att_err && (read_len <= mGattReadBufferSize)) {
    memcpy(mGattReadBuffer, data, read_len);
    mGattReadSize = read_len;
  } else {
    LOG_ERR("Unsuccessful GATT read operation (err %d)", att_err);
  }
  k_poll_signal_raise(&mGattReadSignal, 1);
}

bool BleDevice::Read(struct bt_uuid *uuid, uint8_t *buffer, uint16_t maxReadLength) {
  char str[BT_UUID_STR_LEN];
  bt_uuid_to_str(uuid, str, sizeof(str));
  LOG_INF("Read uuid %s to buffer %p with max length %d", str, buffer, maxReadLength);

  bt_gatt_read_params params;
  params.func = GATTReadCallbackEntry;
  params.handle_count = 1;
  params.single.handle = findHandleByUuid(uuid);
  params.single.offset = 0;

  int err = bt_gatt_read(mConn, &params);
  if (err) {
    LOG_INF("GATT read failed (err %d)", err);
  }

  k_poll_signal_init(&mGattReadSignal);
  k_poll_event_init(mGattWaitEvents, K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &mGattReadSignal);
  k_poll(mGattWaitEvents, ARRAY_SIZE(mGattWaitEvents), K_SECONDS(3));
  k_poll_signal_reset(&mGattReadSignal);

  bool gattReadSuccess = mGattReadSize > 0;
  if (gattReadSuccess && mGattReadSize <= maxReadLength) {
    *buffer = *mGattReadBuffer;
  }

  mGattReadSize = 0;
  LOG_INF("Read success %s", gattReadSuccess ? "true" : "false");
  return gattReadSuccess;
}
