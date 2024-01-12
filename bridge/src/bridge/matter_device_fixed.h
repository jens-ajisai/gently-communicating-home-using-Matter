#pragma once

#include <app/util/af-types.h>
#include <lib/core/DataModelTypes.h>
#include <lib/support/CHIPMem.h>

#include <map>

#include "matter_device.h"

class MatterDeviceFixed : public MatterDevice {
 public:
  static constexpr uint8_t kNodeLabelSize = NODE_LABEL_SIZE;
  
  struct MatterDeviceConfiguration {
    EmberAfEndpointType *ep;
    const chip::Span<const EmberAfDeviceType> *deviceTypes;
    const chip::Span<chip::DataVersion> *dataVersions;
    char name[kNodeLabelSize];
  };

  MatterDeviceFixed(struct MatterDeviceConfiguration &conf) { mConf = conf; };

  ~MatterDeviceFixed() {
  }

  // Interface of matter_device.h
  void Init();
  const char *GetName() { return mConf.name; }
  EmberAfEndpointType *GetEndpoint() { return mConf.ep; }
  const chip::Span<chip::DataVersion> *GetDataVersions() { return mConf.dataVersions; }
  const chip::Span<const EmberAfDeviceType> *GetDeviceTypes() { return mConf.deviceTypes; }

  void onReminderCheck();

 private:
  struct MatterDeviceConfiguration mConf;
};
