#include "bridge_manager.h"

#include <app-common/zap-generated/ids/Clusters.h>
#include <app/reporting/reporting.h>
#include <app/util/af-types.h>
#include <app/util/attribute-storage.h>
#include <lib/core/DataModelTypes.h>
#include <platform/PlatformManager.h>
#include <zephyr/logging/log.h>

#include <map>

#include "ble_connectivity_manager.h"
#include "matter_device.h"
#include "matter_device_ble.h"
LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace ::chip::app::Clusters;

CHIP_ERROR BridgeManager::Init(struct MatterDeviceBle::MatterDeviceConfiguration conf, struct MatterDeviceFixed::MatterDeviceConfiguration conf2) {
  LOG_INF("BridgeManager::Init");
  CHIP_ERROR err;
  // Set starting endpoint id where dynamic endpoints will be assigned, which
  // will be the next consecutive endpoint id after the last fixed endpoint.
  mCurrentEndpointId =
      static_cast<chip::EndpointId>(static_cast<int>(emberAfEndpointFromIndex(
                                        static_cast<uint16_t>(emberAfFixedEndpointCount() - 1))) +
                                    1);

  // Disable last fixed endpoint, which is used as a placeholder for all of the
  // supported clusters so that ZAP will generated the requisite code.
  emberAfEndpointEnableDisable(
      emberAfEndpointFromIndex(static_cast<uint16_t>(emberAfFixedEndpointCount() - 1)), false);

  err = BLEConnectivityManager::Instance().Init();
  if (err != CHIP_NO_ERROR) {
    return err;
  }

  MatterDeviceBle *dev = chip::Platform::New<MatterDeviceBle>(conf);
  err = AddDeviceEndpoint(dev);
  if (err != CHIP_NO_ERROR) {
    return err;
  }

  MatterDeviceFixed *dev2 = chip::Platform::New<MatterDeviceFixed>(conf2);
  err = AddDeviceEndpoint(dev2);
  return err;
}

CHIP_ERROR BridgeManager::AddDeviceEndpoint(MatterDevice *dev) {
  LOG_INF("BridgeManager::AddDeviceEndpoint");

  for (const auto &[key, value] : Instance().mDevicesMap) {
    if (value == dev) {
      LOG_WRN("Device already added!");
      return CHIP_ERROR_INTERNAL;
    }
  }

  chip::DeviceLayer::PlatformMgr().ScheduleWork(
      [](intptr_t context) {
        EmberAfStatus ret;

        uint8_t index = 0;
        while (true) {
          if (!Instance().mDevicesMap.contains(index)) {
            break;
          }
          index++;
        }

        MatterDevice *dev = reinterpret_cast<MatterDevice *>(context);

        dev->SetEndpointId(BridgeManager::Instance().mCurrentEndpointId);
        
        // Register dyanmic matter device
        ret = emberAfSetDynamicEndpoint(index, BridgeManager::Instance().mCurrentEndpointId,
                                        dev->GetEndpoint(), *(dev->GetDataVersions()),
                                        *(dev->GetDeviceTypes()), aggregatorEndpointId);

        if (ret == EMBER_ZCL_STATUS_SUCCESS) {
          LOG_INF("Added device %s to dynamic endpoint %d (index=%d)", dev->GetName(),
                  BridgeManager::Instance().mCurrentEndpointId, index);
          dev->Init();
          BridgeManager::Instance().mDevicesMap[index] = dev;
        }
        BridgeManager::Instance().mCurrentEndpointId++;
      },
      reinterpret_cast<intptr_t>(dev));

  return CHIP_NO_ERROR;
}

void BridgeManager::RemoveDeviceEndpoint(MatterDevice *dev) {
  LOG_INF("BridgeManager::RemoveDeviceEndpoint");
  chip::DeviceLayer::PlatformMgr().ScheduleWork(
      [](intptr_t context) {
        MatterDevice *dev = reinterpret_cast<MatterDevice *>(context);
        auto findResult = std::find_if(
            std::begin(BridgeManager::Instance().mDevicesMap),
            std::end(BridgeManager::Instance().mDevicesMap),
            [&](const std::pair<uint8_t, MatterDevice *> &pair) { return pair.second == dev; });

        uint8_t index = -1;
        if (findResult != std::end(BridgeManager::Instance().mDevicesMap)) {
          index = findResult->first;
        }
        if (index >= 0) {
          chip::EndpointId ep = emberAfClearDynamicEndpoint((uint16_t)index);
          LOG_INF("Remove device %s from dynamic endpoint %d (index=%d)", dev->GetName(), ep,
                  (uint16_t)index);
          chip::Platform::Delete(dev);
          BridgeManager::Instance().mDevicesMap.erase(index);
        }
      },
      reinterpret_cast<intptr_t>(dev));
}

void BridgeManager::HandleUpdate(chip::app::ConcreteAttributePath *path) {
  LOG_INF("BridgeManager::HandleUpdate");
  MatterReportingAttributeChangeCallback(*path);
  chip::Platform::Delete(path);
}

// Value read/write Matter -> BleDevice
CHIP_ERROR BridgeManager::HandleRead(uint16_t index, chip::ClusterId clusterId,
                                     const EmberAfAttributeMetadata *attributeMetadata,
                                     uint8_t *buffer, uint16_t maxReadLength) {
  LOG_INF("BridgeManager::HandleRead");
  VerifyOrReturnError(attributeMetadata && buffer, CHIP_ERROR_INVALID_ARGUMENT,
                      LOG_ERR("No attributeMetadata or buffer."));
  VerifyOrReturnValue(Instance().mDevicesMap.contains(index), CHIP_ERROR_INTERNAL,
                      LOG_ERR("No device for index %d", index));

  auto *device = Instance().mDevicesMap[index];

  /* Handle reads for the generic information for all bridged devices. Provide a valid answer even
   * if device state is unreachable. */
  switch (clusterId) {
    case BridgedDeviceBasicInformation::Id:
      return device->HandleReadBridgedDeviceBasicInformation(attributeMetadata->attributeId, buffer,
                                                             maxReadLength);
    case Descriptor::Id:
      return device->HandleReadDescriptor(attributeMetadata->attributeId, buffer, maxReadLength);
    default:
      break;
  }

  /* Verify if the device is reachable or we should return prematurely. */
  VerifyOrReturnError(device->GetIsReachable(), CHIP_ERROR_INCORRECT_STATE,
                      LOG_ERR("Device is not reachable."));

  return device->HandleRead(clusterId, attributeMetadata->attributeId, buffer, maxReadLength);
}

CHIP_ERROR BridgeManager::HandleWrite(uint16_t index, chip::ClusterId clusterId,
                                      const EmberAfAttributeMetadata *attributeMetadata,
                                      uint8_t *buffer) {
  LOG_INF("BridgeManager::HandleRead");
  VerifyOrReturnError(attributeMetadata && buffer, CHIP_ERROR_INVALID_ARGUMENT,
                      LOG_ERR("No attributeMetadata or buffer."));
  VerifyOrReturnValue(Instance().mDevicesMap.contains(index), CHIP_ERROR_INTERNAL,
                      LOG_ERR("No device for index %d", index));

  auto *device = Instance().mDevicesMap[index];

  /* Verify if the device is reachable or we should return prematurely. */
  VerifyOrReturnError(device->GetIsReachable(), CHIP_ERROR_INCORRECT_STATE,
                      LOG_ERR("Device is not reachable."));

  CHIP_ERROR err = device->HandleWrite(clusterId, attributeMetadata->attributeId, buffer);

  return err;
}

// Matter read/write requests
EmberAfStatus emberAfExternalAttributeReadCallback(
    chip::EndpointId endpoint, chip::ClusterId clusterId,
    const EmberAfAttributeMetadata *attributeMetadata, uint8_t *buffer, uint16_t maxReadLength) {
  uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(endpoint);
  LOG_INF("emberAfExternalAttributeReadCallback with endpoint %d, clusterId %d, endpointIndex %d",
          endpoint, clusterId, endpointIndex);

  if (CHIP_NO_ERROR == BridgeManager::Instance().HandleRead(
                           endpointIndex, clusterId, attributeMetadata, buffer, maxReadLength)) {
    return EMBER_ZCL_STATUS_SUCCESS;
  } else {
    return EMBER_ZCL_STATUS_FAILURE;
  }
}

EmberAfStatus emberAfExternalAttributeWriteCallback(
    chip::EndpointId endpoint, chip::ClusterId clusterId,
    const EmberAfAttributeMetadata *attributeMetadata, uint8_t *buffer) {
  uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(endpoint);
  LOG_INF("emberAfExternalAttributeWriteCallback with endpoint %d, clusterId %d, endpointIndex %d",
          endpoint, clusterId, endpointIndex);

  if (CHIP_NO_ERROR ==
      BridgeManager::Instance().HandleWrite(endpointIndex, clusterId, attributeMetadata, buffer)) {
    return EMBER_ZCL_STATUS_SUCCESS;
  } else {
    return EMBER_ZCL_STATUS_FAILURE;
  }
}
