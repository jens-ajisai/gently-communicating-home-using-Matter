#include "matter_device_ble.h"

#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/util/af-types.h>
#include <lib/core/DataModelTypes.h>
#include <platform/PlatformManager.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>

#include "ble_device.h"
#include "bridge_manager.h"
LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace ::chip::app::Clusters;

/****************************
 * Helper functions to retrieve information from mapping
 ****************************/
std::pair<chip::ClusterId, chip::AttributeId> MatterDeviceBle::findClusterAttributeIdByUuid(
    struct bt_uuid *uuid) {
  auto findResult =
      std::find_if(std::begin(mConf.matterBleMapping), std::end(mConf.matterBleMapping),
                   [&](const std::pair<std::pair<chip::ClusterId, chip::AttributeId>,
                                       std::pair<AttributeTypes, bt_uuid *>> &pair) {
                     return bt_uuid_cmp(uuid, pair.second.second) == 0;
                   });

  std::pair<chip::ClusterId, chip::AttributeId> foundKey = {0, 0};
  if (findResult != std::end(mConf.matterBleMapping)) {
    foundKey = findResult->first;
  }

  char str[BT_UUID_STR_LEN];
  bt_uuid_to_str(uuid, str, sizeof(str));
  LOG_INF("Found ClusterId %d, AttributeId %d for uuid %s", foundKey.first, foundKey.second, str);
  return foundKey;
}

/****************************
 * Free functions to map to member callbacks to workaround not being able to give member functions
 * to c-style callbacks
 ****************************/
static void ConnectedCallbackEntry(void *ctx, bool connected, struct bt_conn *conn,
                                   struct bt_uuid *serviceUuid) {
  MatterDeviceBle *dev = reinterpret_cast<MatterDeviceBle *>(ctx);
  dev->ConnectedCallback(connected, conn, serviceUuid);
}

static void DiscoveredCallbackEntry(void *ctx) {
  MatterDeviceBle *dev = reinterpret_cast<MatterDeviceBle *>(ctx);
  dev->DiscoveredCallback();
}

static void SubscriptionCallbackEntry(void *ctx, struct bt_uuid *charUuid, const void *data,
                                      uint16_t length) {
  MatterDeviceBle *dev = reinterpret_cast<MatterDeviceBle *>(ctx);
  dev->SubscriptionCallback(charUuid, data, length);
}

static void RecoveryTimeoutCallbackEntry(k_timer *timer) {
  MatterDeviceBle *dev = reinterpret_cast<MatterDeviceBle *>(k_timer_user_data_get(timer));

  chip::DeviceLayer::PlatformMgr().ScheduleWork(
      [](intptr_t context) {
        MatterDeviceBle *dev = reinterpret_cast<MatterDeviceBle *>(context);
        dev->RecoveryTimeoutCallback();
      },
      reinterpret_cast<intptr_t>(dev));
}

/****************************
 * Callback member functions
 ****************************/
void MatterDeviceBle::SubscriptionCallback(struct bt_uuid *charUuid, const void *data,
                                        uint16_t length) {
  char str[BT_UUID_STR_LEN];
  bt_uuid_to_str(charUuid, str, sizeof(str));
  LOG_INF("SubscriptionCallback for attribute uuid %s", str);
  LOG_HEXDUMP_DBG(data, length, "SubscriptionCallback data");
  auto clusterAndAttributeId = findClusterAttributeIdByUuid(charUuid);
  chip::ClusterId clusterId = clusterAndAttributeId.first;
  chip::AttributeId attributeId = clusterAndAttributeId.second;
  chip::EndpointId endpointId = GetEndpointId();

  auto path =
      chip::Platform::New<chip::app::ConcreteAttributePath>(endpointId, clusterId, attributeId);
  LOG_INF("path address %p", path);
  VerifyOrReturn(data, LOG_ERR("SubscriptionCallback: No data."));
  VerifyOrReturn(length == sizeof(uint16_t),
                 LOG_ERR("SubscriptionCallback: Unexpected length %d. Expected: %d", length,
                         sizeof(uint16_t)));

  const uint16_t *value = reinterpret_cast<const uint16_t *>(data);

  // Cache received data
  attributeCache[attributeId] = *value;
  chip::DeviceLayer::PlatformMgr().ScheduleWork(
      [](intptr_t context) {
        auto path = reinterpret_cast<chip::app::ConcreteAttributePath *>(context);
        BridgeManager::Instance().HandleUpdate(path);
      },
      reinterpret_cast<intptr_t>(path));
}

void MatterDeviceBle::DiscoveredCallback() {
  LOG_INF("MatterDeviceBle::DiscoveredCallback");

  for (const auto &attr : mConf.matterBleMapping) {
    if (attr.second.first == AttributeTypes::READ_ONCE) {
      uint16_t buffer;
      if (mBleDevice->Read(attr.second.second, (uint8_t *)&buffer, 2)) {
        attributeCache[attr.first.second] = buffer;
      } else {
        LOG_ERR("Read error. Disconnect");
        mBleDevice->Disconnect();
      }
    } else if (attr.second.first == AttributeTypes::SUBSCRIBE) {
      mBleDevice->Subscribe(this, SubscriptionCallbackEntry, attr.second.second);
    }
  }
  SetIsReachable(true);
}

void MatterDeviceBle::ConnectedCallback(bool connected, struct bt_conn *conn,
                                     struct bt_uuid *serviceUuid) {
  char str[BT_UUID_STR_LEN];
  bt_uuid_to_str(serviceUuid, str, sizeof(str));
  LOG_INF("MatterDeviceBle::ConnectedCallback connected %d to service uuid %s", connected, str);

  if (conn == nullptr) {
    // no devices found
    LOG_INF("Scan did not find any device.");
    k_timer_start(&mRecoveryTimer, K_MSEC(kRecoveryDelayMs), K_NO_WAIT);
  } else if (connected) {
    // found device and automatically connected.
    mBleDevice = chip::Platform::New<BleDevice>(conn);

    mBleDevice->Discover(this, DiscoveredCallbackEntry, serviceUuid);
  } else {
    // device got disconnected
    LOG_INF("Delete mBleDevice");
    if (mBleDevice) {
      chip::Platform::Delete(mBleDevice);
      mBleDevice = NULL;
    }

    SetIsReachable(false);
//    BridgeManager::Instance().RemoveDeviceEndpoint(this);

    LOG_INF("Deleted mBleDevice");
    k_timer_start(&mRecoveryTimer, K_MSEC(kRecoveryDelayMs), K_NO_WAIT);
  }
}

void MatterDeviceBle::RecoveryTimeoutCallback() {
  LOG_INF("MatterDeviceBle::RecoveryTimeoutCallback");
  // If there is a bonded device then connect
  // If there is no bonded device then re-scan but less frequent
  // If there are multiple bonded devices then ?? -> not implemented
  if (!BLEConnectivityManager::Instance().ConnectFirstBondedDevice(this, ConnectedCallbackEntry,
                                                                   mConf.filter)) {
    BLEConnectivityManager::Instance().Scan(this, ConnectedCallbackEntry, mConf.filter,
                                            kRecoveryScanTimeoutMs);
  }
}

/****************************
 * Init
 ****************************/
void MatterDeviceBle::Init() {
  LOG_INF("MatterDeviceBle::Init");

  k_timer_init(&mRecoveryTimer, RecoveryTimeoutCallbackEntry, nullptr);
  k_timer_user_data_set(&mRecoveryTimer, this);

  if (!BLEConnectivityManager::Instance().ConnectFirstBondedDevice(this, ConnectedCallbackEntry,
                                                                   mConf.filter)) {
    BLEConnectivityManager::Instance().Scan(this, ConnectedCallbackEntry, mConf.filter);
  }

  auto path = chip::Platform::New<chip::app::ConcreteAttributePath>(
      GetEndpointId(), BridgedDeviceBasicInformation::Id,
      BridgedDeviceBasicInformation::Attributes::NodeLabel::Id);
  chip::DeviceLayer::PlatformMgr().ScheduleWork(
      [](intptr_t context) {
        auto path = reinterpret_cast<chip::app::ConcreteAttributePath *>(context);
        BridgeManager::Instance().HandleUpdate(path);
      },
      reinterpret_cast<intptr_t>(path));
}
