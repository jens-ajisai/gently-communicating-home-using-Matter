#pragma once

#include <app/util/af-types.h>
#include <lib/core/DataModelTypes.h>
#include <lib/support/CHIPMem.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>

#include <map>

#include "matter_device.h"
#include "ble_connectivity_manager.h"
#include "ble_device.h"

#define BT_SERVICE_UUID_CNT 1
#define BT_CHARACTERISTIC_UUID_CNT 8

class MatterDeviceBle : public MatterDevice {
 public:
  static constexpr uint16_t kRecoveryDelayMs = CONFIG_BRIDGE_BT_RECOVERY_INTERVAL_MS;
  static constexpr uint16_t kRecoveryScanTimeoutMs = CONFIG_BRIDGE_BT_RECOVERY_SCAN_TIMEOUT_MS;
  static constexpr uint8_t kNodeLabelSize = NODE_LABEL_SIZE;

  enum AttributeTypes { READ_ONCE, SUBSCRIBE };

  struct MatterDeviceConfiguration {
    EmberAfEndpointType *ep;
    const chip::Span<const EmberAfDeviceType> *deviceTypes;
    const chip::Span<chip::DataVersion> *dataVersions;
    char name[kNodeLabelSize];
    BLEConnectivityManager::DeviceFilter filter;
    std::map<std::pair<chip::ClusterId, chip::AttributeId>,
             std::pair<MatterDeviceBle::AttributeTypes, bt_uuid *>>
        matterBleMapping;
  };

  MatterDeviceBle(struct MatterDeviceConfiguration &conf) { mConf = conf; };

  ~MatterDeviceBle() {
    if (mBleDevice) chip::Platform::Delete(mBleDevice);
  }

  // Callback functions for Ble
  void SubscriptionCallback(struct bt_uuid *charUuid, const void *data, uint16_t length);
  void DiscoveredCallback();
  void ConnectedCallback(bool connected, struct bt_conn *conn, struct bt_uuid *serviceUuid);

  void RecoveryTimeoutCallback();

  // Interface of matter_device.h
  void Init();
  const char *GetName() { return mConf.name; }
  EmberAfEndpointType *GetEndpoint() { return mConf.ep; }
  const chip::Span<chip::DataVersion> *GetDataVersions() { return mConf.dataVersions; }
  const chip::Span<const EmberAfDeviceType> *GetDeviceTypes() { return mConf.deviceTypes; }

 private:
  struct MatterDeviceConfiguration mConf;

  std::pair<chip::ClusterId, chip::AttributeId> findClusterAttributeIdByUuid(struct bt_uuid *uuid);

  BleDevice *mBleDevice;

  k_timer mRecoveryTimer;
};
