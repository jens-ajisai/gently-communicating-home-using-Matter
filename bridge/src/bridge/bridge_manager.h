#pragma once

#include <app/util/af-types.h>
#include <lib/core/DataModelTypes.h>

#include <map>

#include "matter_device.h"
#include "matter_device_ble.h"
#include "matter_device_fixed.h"

class BridgeManager {
 public:
  // https://github.com/nrfconnect/sdk-nrf/commit/390c3f93d63444f39477ecc6a5dfd43caa773152
  static constexpr chip::EndpointId aggregatorEndpointId = CONFIG_BRIDGE_AGGREGATOR_ENDPOINT_ID;

  CHIP_ERROR Init(struct MatterDeviceBle::MatterDeviceConfiguration conf, struct MatterDeviceFixed::MatterDeviceConfiguration conf2);

  static BridgeManager &Instance() {
    static BridgeManager sInstance;
    return sInstance;
  }

  void HandleUpdate(chip::app::ConcreteAttributePath *path);

  CHIP_ERROR HandleRead(uint16_t index, chip::ClusterId clusterId,
                        const EmberAfAttributeMetadata *attributeMetadata, uint8_t *buffer,
                        uint16_t maxReadLength);
  CHIP_ERROR HandleWrite(uint16_t index, chip::ClusterId clusterId,
                         const EmberAfAttributeMetadata *attributeMetadata, uint8_t *buffer);

  chip::EndpointId mCurrentEndpointId;
  std::map<uint8_t, MatterDevice *> mDevicesMap;

  void RemoveDeviceEndpoint(MatterDevice *dev);

 private:
  CHIP_ERROR AddDeviceEndpoint(MatterDevice *dev);
};
