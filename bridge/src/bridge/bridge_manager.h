#pragma once

#include <app/util/af-types.h>
#include <lib/core/DataModelTypes.h>

#include <map>

#include "matter_device.h"

class BridgeManager {
 public:
  CHIP_ERROR Init(struct MatterDevice::MatterDeviceConfiguration conf);

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
