#pragma once

#include <app/util/af-types.h>
#include <lib/core/DataModelTypes.h>
#include <lib/support/CHIPMem.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>

#include <map>

#include "ble_connectivity_manager.h"
#include "ble_device.h"

#define BT_SERVICE_UUID_CNT 1
#define BT_CHARACTERISTIC_UUID_CNT 8
#define BT_NODE_LABEL_SIZE 32

class MatterDevice {
 public:
  static constexpr uint8_t kNodeLabelSize = BT_NODE_LABEL_SIZE;
  static constexpr uint16_t kRecoveryDelayMs = CONFIG_BRIDGE_BT_RECOVERY_INTERVAL_MS;
  static constexpr uint16_t kRecoveryScanTimeoutMs = CONFIG_BRIDGE_BT_RECOVERY_SCAN_TIMEOUT_MS;

  enum AttributeTypes { READ_ONCE, SUBSCRIBE };

  struct MatterDeviceConfiguration {
    chip::ClusterId clusterId;
    chip::EndpointId endpointId;
    EmberAfEndpointType *ep;
    const chip::Span<const EmberAfDeviceType> *deviceTypes;
    const chip::Span<chip::DataVersion> *dataVersions;
    char name[kNodeLabelSize];
    BLEConnectivityManager::DeviceFilter filter;
    std::map<std::pair<chip::ClusterId, chip::AttributeId>,
             std::pair<MatterDevice::AttributeTypes, bt_uuid *>>
        matterBleMapping;
  };

  MatterDevice(struct MatterDeviceConfiguration &conf) { mConf = conf; };

  ~MatterDevice() {
    if (mBleDevice) chip::Platform::Delete(mBleDevice);
  }

  void Init();

  // Callback functions for Ble
  void SubscriptionCallback(struct bt_uuid *charUuid, const void *data, uint16_t length);
  void DiscoveredCallback();
  void ConnectedCallback(bool connected, struct bt_conn *conn, struct bt_uuid *serviceUuid);

  void RecoveryTimeoutCallback();

  // Callback functions for Matter
  CHIP_ERROR HandleReadBridgedDeviceBasicInformation(chip::AttributeId attributeId, uint8_t *buffer,
                                                     uint16_t maxReadLength);
  CHIP_ERROR HandleReadDescriptor(chip::AttributeId attributeId, uint8_t *buffer,
                                  uint16_t maxReadLength);
  CHIP_ERROR HandleRead(chip::ClusterId clusterId, chip::AttributeId attributeId, uint8_t *buffer,
                        uint16_t maxReadLength);
  CHIP_ERROR HandleWrite(chip::ClusterId clusterId, chip::AttributeId attributeId, uint8_t *buffer);

  inline void SetEndpointId(chip::EndpointId id) { mConf.endpointId = id; };
  inline chip::EndpointId GetEndpointId() { return mConf.endpointId; };
  bool GetIsReachable() const { return mIsReachable; }
  const char *GetName() { return mConf.name; }

  struct MatterDeviceConfiguration mConf;

 private:
  void SetIsReachable(bool isReachable);

  std::pair<chip::ClusterId, chip::AttributeId> findClusterAttributeIdByUuid(struct bt_uuid *uuid);

  bool mIsReachable = true;
  BleDevice *mBleDevice;
  std::map<chip::AttributeId, uint16_t> attributeCache;

  k_timer mRecoveryTimer;
};
