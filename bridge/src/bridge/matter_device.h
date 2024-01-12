#pragma once

#include <app/util/af-types.h>
#include <lib/core/DataModelTypes.h>
#include <lib/support/CHIPMem.h>
#include <zephyr/kernel.h>

#include <map>

#define NODE_LABEL_SIZE 32

class MatterDevice {
 public:
  virtual void Init() = 0;

  virtual const char *GetName() = 0;
  virtual EmberAfEndpointType *GetEndpoint() = 0;
  virtual const chip::Span<chip::DataVersion> *GetDataVersions() = 0;
  virtual const chip::Span<const EmberAfDeviceType> *GetDeviceTypes() = 0;

  // Callback functions for Matter
  CHIP_ERROR HandleReadBridgedDeviceBasicInformation(chip::AttributeId attributeId, uint8_t *buffer,
                                                     uint16_t maxReadLength);
  CHIP_ERROR HandleReadDescriptor(chip::AttributeId attributeId, uint8_t *buffer,
                                  uint16_t maxReadLength);
  CHIP_ERROR HandleRead(chip::ClusterId clusterId, chip::AttributeId attributeId, uint8_t *buffer,
                        uint16_t maxReadLength);
  CHIP_ERROR HandleWrite(chip::ClusterId clusterId, chip::AttributeId attributeId, uint8_t *buffer);

  std::map<chip::AttributeId, uint16_t> attributeCache;

  void SetEndpointId(chip::EndpointId id) { mEndpointId = id; };
  chip::EndpointId GetEndpointId() { return mEndpointId; };

  void SetIsReachable(bool isReachable);
  bool GetIsReachable() const { return mIsReachable; }

 protected:
  bool mIsReachable = true;
  chip::EndpointId mEndpointId;
};
