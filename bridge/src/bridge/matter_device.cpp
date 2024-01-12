#include "matter_device.h"

#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/util/af-types.h>
#include <lib/core/DataModelTypes.h>
#include <lib/support/ZclString.h>
#include <platform/PlatformManager.h>
#include <zephyr/logging/log.h>

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
      break;
    }
    case BridgedDeviceBasicInformation::Attributes::NodeLabel::Id: {
      chip::MutableByteSpan zclNodeLabelSpan(buffer, maxReadLength);
      return MakeZclCharString(zclNodeLabelSpan, GetName());
    }
    case BridgedDeviceBasicInformation::Attributes::ClusterRevision::Id: {
      uint16_t clusterRevision = ZCL_BRIDGED_DEVICE_BASIC_INFORMATION_CLUSTER_REVISION;
      memcpy(buffer, &clusterRevision, sizeof(clusterRevision));
      break;
    }
    case BridgedDeviceBasicInformation::Attributes::FeatureMap::Id: {
      uint32_t featureMap = ZCL_BRIDGED_DEVICE_BASIC_INFORMATION_FEATURE_MAP;
      memcpy(buffer, &featureMap, sizeof(featureMap));
      break;
    }
    default:
      LOG_ERR("MatterDevice::HandleReadBridgedDeviceBasicInformation attribute Id %d not handled",
              attributeId);
      return CHIP_ERROR_INVALID_ARGUMENT;
  }
  return CHIP_NO_ERROR;
}

CHIP_ERROR MatterDevice::HandleReadDescriptor(chip::AttributeId attributeId, uint8_t *buffer,
                                              uint16_t maxReadLength) {
  LOG_INF("MatterDevice::HandleReadDescriptor with attributeId %d", attributeId);
  switch (attributeId) {
    case Descriptor::Attributes::ClusterRevision::Id: {
      uint16_t clusterRevision = ZCL_CLUSTER_REVISION;
      memcpy(buffer, &clusterRevision, sizeof(clusterRevision));
      break;
    }
    case Descriptor::Attributes::FeatureMap::Id: {
      uint32_t featureMap = ZCL_FEATURE_MAP;
      memcpy(buffer, &featureMap, sizeof(featureMap));
      break;
    }
    default:
      LOG_ERR("MatterDevice::HandleReadDescriptor attribute Id %d not handled", attributeId);
      return CHIP_ERROR_INVALID_ARGUMENT;
  }
  return CHIP_NO_ERROR;
}

CHIP_ERROR MatterDevice::HandleRead(chip::ClusterId clusterId, chip::AttributeId attributeId,
                                    uint8_t *buffer, uint16_t maxReadLength) {
  LOG_INF("MatterDevice::HandleRead with clusterId %d, attributeId %d", clusterId, attributeId);
  switch (attributeId) {
    case LevelControl::Attributes::ClusterRevision::Id: {
      uint16_t clusterRevision = ZCL_CLUSTER_REVISION;
      memcpy(buffer, &clusterRevision, sizeof(clusterRevision));
      break;
    }
    case LevelControl::Attributes::FeatureMap::Id: {
      uint32_t featureMap = ZCL_FEATURE_MAP;
      memcpy(buffer, &featureMap, sizeof(featureMap));
      break;
    }
    default: {
      LOG_INF("MatterDevice::HandleRead forward to device");
      if (attributeCache.contains(attributeId)) {
        memcpy(buffer, &attributeCache[attributeId], 2);
        LOG_INF("MatterDevice::HandleRead Read OK");
      } else {
        LOG_ERR("MatterDevice::HandleRead Read No value cached.");
        // Temporarily workaround to return CHIP_NO_ERROR
        memset(buffer, 0, maxReadLength);
        // return CHIP_ERROR_INTERNAL;
      }
      break;
    }
  }
  return CHIP_NO_ERROR;
}

/*
<inf> app: MatterDevice::HandleRead with clusterId 6, attributeId 0 OnOff OnOff
<inf> app: MatterDevice::HandleRead with clusterId 8, attributeId 0 CurrentLevel
<inf> app: MatterDevice::HandleRead with clusterId 8, attributeId 1 RemainingTime
<inf> app: MatterDevice::HandleRead with clusterId 8, attributeId 2 MinLevel
<inf> app: MatterDevice::HandleRead with clusterId 8, attributeId 3 MaxLevel
<inf> app: MatterDevice::HandleRead with clusterId 8, attributeId 15 Options
<inf> app: MatterDevice::HandleRead with clusterId 8, attributeId 17 OnLevel
<inf> app: MatterDevice::HandleRead with clusterId 8, attributeId 16384 StartUpCurrentLevel

*/

CHIP_ERROR MatterDevice::HandleWrite(chip::ClusterId clusterId, chip::AttributeId attributeId,
                                     uint8_t *buffer) {
  LOG_INF("MatterDevice::HandleWrite with clusterId %d, attributeId %d", clusterId, attributeId);
  return CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE;
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
