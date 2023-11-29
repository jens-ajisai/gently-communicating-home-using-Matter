#include "matter_device.h"

#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/util/af-types.h>
#include <lib/core/DataModelTypes.h>
#include <lib/support/ZclString.h>
#include <platform/PlatformManager.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>

#include "ble_device.h"
#include "bridge_manager.h"
LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

#define ZCL_CLUSTER_REVISION (1u)
#define ZCL_FEATURE_MAP (0u)
#define ZCL_BRIDGED_DEVICE_BASIC_INFORMATION_CLUSTER_REVISION (1u)
#define ZCL_BRIDGED_DEVICE_BASIC_INFORMATION_FEATURE_MAP (0u)
#define ZCL_ON_OFF_CLUSTER_REVISION (4u)

using namespace ::chip::app::Clusters;

/****************************
 * Matter functions to read attributes
 ****************************/
CHIP_ERROR MatterDevice::HandleReadBridgedDeviceBasicInformation(chip::AttributeId attributeId,
                                                                 uint8_t *buffer,
                                                                 uint16_t maxReadLength) {
  LOG_INF("MatterDevice::HandleReadBridgedDeviceBasicInformation with attributeId %d", attributeId);
  switch (attributeId) {
    case BridgedDeviceBasicInformation::Attributes::Reachable::Id: {
      *buffer = GetIsReachable() ? 1 : 0;
    }
    case BridgedDeviceBasicInformation::Attributes::NodeLabel::Id: {
      chip::MutableByteSpan zclNodeLabelSpan(buffer, maxReadLength);
      return MakeZclCharString(zclNodeLabelSpan, mConf.name);
    }
    case BridgedDeviceBasicInformation::Attributes::ClusterRevision::Id: {
      uint16_t clusterRevision = ZCL_BRIDGED_DEVICE_BASIC_INFORMATION_CLUSTER_REVISION;
      memcpy(buffer, &clusterRevision, sizeof(clusterRevision));
    }
    case BridgedDeviceBasicInformation::Attributes::FeatureMap::Id: {
      uint32_t featureMap = ZCL_BRIDGED_DEVICE_BASIC_INFORMATION_FEATURE_MAP;
      memcpy(buffer, &featureMap, sizeof(featureMap));
    }
    default:
      LOG_ERR("MatterDevice::HandleReadBridgedDeviceBasicInformation attribute Id %d not handled",
              attributeId);
      return CHIP_ERROR_INVALID_ARGUMENT;
  }
}

CHIP_ERROR MatterDevice::HandleReadDescriptor(chip::AttributeId attributeId, uint8_t *buffer,
                                              uint16_t maxReadLength) {
  LOG_INF("MatterDevice::HandleReadDescriptor with attributeId %d", attributeId);
  switch (attributeId) {
    case Descriptor::Attributes::ClusterRevision::Id: {
      uint16_t clusterRevision = ZCL_CLUSTER_REVISION;
      memcpy(buffer, &clusterRevision, sizeof(clusterRevision));
    }
    case Descriptor::Attributes::FeatureMap::Id: {
      uint32_t featureMap = ZCL_FEATURE_MAP;
      memcpy(buffer, &featureMap, sizeof(featureMap));
    }
    default:
      LOG_ERR("MatterDevice::HandleReadDescriptor attribute Id %d not handled", attributeId);
      return CHIP_ERROR_INVALID_ARGUMENT;
  }
}

CHIP_ERROR MatterDevice::HandleRead(chip::ClusterId clusterId, chip::AttributeId attributeId,
                                    uint8_t *buffer, uint16_t maxReadLength) {
  LOG_INF("MatterDevice::HandleRead with clusterId %d, attributeId %d", clusterId, attributeId);
  switch (attributeId) {
    case LevelControl::Attributes::ClusterRevision::Id: {
      uint16_t clusterRevision = ZCL_CLUSTER_REVISION;
      memcpy(buffer, &clusterRevision, sizeof(clusterRevision));
    }
    case LevelControl::Attributes::FeatureMap::Id: {
      uint32_t featureMap = ZCL_FEATURE_MAP;
      memcpy(buffer, &featureMap, sizeof(featureMap));
    }
    default: {
      LOG_INF("MatterDevice::HandleRead forward to device");
      if (attributeCache.contains(attributeId)) {
        memcpy(buffer, &attributeCache[attributeId], 2);
        LOG_INF("MatterDevice::HandleRead Read OK");
        return CHIP_NO_ERROR;
      } else {
        LOG_ERR("MatterDevice::HandleRead Read No value cached.");
        return CHIP_ERROR_INTERNAL;
      }
    }
  }
  return CHIP_NO_ERROR;
}

CHIP_ERROR MatterDevice::HandleWrite(chip::ClusterId clusterId, chip::AttributeId attributeId,
                                     uint8_t *buffer) {
  LOG_INF("MatterDevice::HandleWrite with clusterId %d, attributeId %d", clusterId, attributeId);
  return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
}

/****************************
 * Helper functions to retrieve information from mapping
 ****************************/
std::pair<chip::ClusterId, chip::AttributeId> MatterDevice::findClusterAttributeIdByUuid(
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
  MatterDevice *dev = reinterpret_cast<MatterDevice *>(ctx);
  dev->ConnectedCallback(connected, conn, serviceUuid);
}

static void DiscoveredCallbackEntry(void *ctx) {
  MatterDevice *dev = reinterpret_cast<MatterDevice *>(ctx);
  dev->DiscoveredCallback();
}

static void SubscriptionCallbackEntry(void *ctx, struct bt_uuid *charUuid, const void *data,
                                      uint16_t length) {
  MatterDevice *dev = reinterpret_cast<MatterDevice *>(ctx);
  dev->SubscriptionCallback(charUuid, data, length);
}

static void RecoveryTimeoutCallbackEntry(k_timer *timer) {
  MatterDevice *dev = reinterpret_cast<MatterDevice *>(k_timer_user_data_get(timer));

  chip::DeviceLayer::PlatformMgr().ScheduleWork(
      [](intptr_t context) {
        MatterDevice *dev = reinterpret_cast<MatterDevice *>(context);
        dev->RecoveryTimeoutCallback();
      },
      reinterpret_cast<intptr_t>(dev));
}

/****************************
 * Callback member functions
 ****************************/
void MatterDevice::SubscriptionCallback(struct bt_uuid *charUuid, const void *data,
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

void MatterDevice::DiscoveredCallback() {
  LOG_INF("MatterDevice::DiscoveredCallback");

  for (const auto &attr : mConf.matterBleMapping) {
    if (attr.second.first == AttributeTypes::READ_ONCE) {
      uint16_t buffer;
      if (mBleDevice->Read(attr.second.second, (uint8_t *)&buffer, 2)) {
        attributeCache[attr.first.second] = buffer;
      } else {
        LOG_ERR("Read error");
      }
    } else if (attr.second.first == AttributeTypes::SUBSCRIBE) {
      mBleDevice->Subscribe(this, SubscriptionCallbackEntry, attr.second.second);
    }
  }

  SetIsReachable(true);
}

void MatterDevice::ConnectedCallback(bool connected, struct bt_conn *conn,
                                     struct bt_uuid *serviceUuid) {
  char str[BT_UUID_STR_LEN];
  bt_uuid_to_str(serviceUuid, str, sizeof(str));
  LOG_INF("MatterDevice::ConnectedCallback connected %d to sertice uuid %s", connected, str);

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

void MatterDevice::RecoveryTimeoutCallback() {
  LOG_INF("MatterDevice::RecoveryTimeoutCallback");
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
void MatterDevice::Init() {
  LOG_INF("MatterDevice::Init");

  k_timer_init(&mRecoveryTimer, RecoveryTimeoutCallbackEntry, nullptr);
  k_timer_user_data_set(&mRecoveryTimer, this);

  if (!BLEConnectivityManager::Instance().ConnectFirstBondedDevice(this, ConnectedCallbackEntry,
                                                                   mConf.filter)) {
    BLEConnectivityManager::Instance().ExchangeOob();
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

void MatterDevice::SetIsReachable(bool isReachable) {
  mIsReachable = isReachable;
  auto path = chip::Platform::New<chip::app::ConcreteAttributePath>(
      GetEndpointId(), BridgedDeviceBasicInformation::Id,
      BridgedDeviceBasicInformation::Attributes::Reachable::Id);
  chip::DeviceLayer::PlatformMgr().ScheduleWork(
      [](intptr_t context) {
        auto path = reinterpret_cast<chip::app::ConcreteAttributePath *>(context);
        BridgeManager::Instance().HandleUpdate(path);
      },
      reinterpret_cast<intptr_t>(path));
}
