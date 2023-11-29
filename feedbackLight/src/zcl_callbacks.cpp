/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/CommandHandler.h>
#include <app/ConcreteAttributePath.h>
#include <app/ConcreteCommandPath.h>
#include <lib/support/logging/CHIPLogging.h>

#include "app_task.h"

#define MIN_HUE_VALUE 0
#define MAX_HUE_VALUE 254

#define MIN_SATURATION_VALUE 0
#define MAX_SATURATION_VALUE 254

using namespace ::chip;
using namespace ::chip::app::Clusters;
using namespace ::chip::Protocols::InteractionModel;

void MatterPostAttributeChangeCallback(const app::ConcreteAttributePath &attributePath,
                                       uint8_t type, uint16_t size, uint8_t *value) {
  ClusterId clusterId = attributePath.mClusterId;
  AttributeId attributeId = attributePath.mAttributeId;

  if (clusterId == OnOff::Id && attributeId == OnOff::Attributes::OnOff::Id) {
    ChipLogProgress(Zcl, "Cluster OnOff: attribute OnOff set to %" PRIu8 "", *value);
    AppEvent event;
    event.Type = AppEventType::Lighting;
    event.LightingEvent.Action = LightingAction::SetEnable;
    event.LightingEvent.Value = *value;
    AppTask::Instance().ZCLHandler(event);
  } else if (clusterId == LevelControl::Id &&
             attributeId == LevelControl::Attributes::CurrentLevel::Id) {
    ChipLogProgress(Zcl, "Cluster LevelControl: attribute CurrentLevel set to %" PRIu8 "", *value);
    AppEvent event;
    event.Type = AppEventType::Lighting;
    event.LightingEvent.Action = LightingAction::SetLevel;
    event.LightingEvent.Value = *value;
    AppTask::Instance().ZCLHandler(event);
  } else if (clusterId == ColorControl::Id &&
             attributeId == ColorControl::Attributes::CurrentHue::Id) {
    ChipLogProgress(Zcl, "Cluster LevelControl: attribute CurrentHue set to %" PRIu8 "", *value);
    AppEvent event;
    event.Type = AppEventType::Lighting;
    event.LightingEvent.Action = LightingAction::SetHue;
    event.LightingEvent.Value = *value;
    event.LightingEvent.isRelative = false;
    AppTask::Instance().ZCLHandler(event);
  } else if (clusterId == ColorControl::Id &&
             attributeId == ColorControl::Attributes::CurrentSaturation::Id) {
    ChipLogProgress(Zcl, "Cluster LevelControl: attribute CurrentSaturation set to %" PRIu8 "",
                    *value);
    AppEvent event;
    event.Type = AppEventType::Lighting;
    event.LightingEvent.Action = LightingAction::SetSaturation;
    event.LightingEvent.Value = *value;
    event.LightingEvent.isRelative = false;
    AppTask::Instance().ZCLHandler(event);
  } else {
    ChipLogProgress(
        Zcl, "MatterPostAttributeChangeCallback(clusterId=%d, attributeId=%d) NOT HANDLED!!!",
        clusterId, attributeId);
  }
}

/** @brief OnOff Cluster Init
 *
 * This function is called when a specific cluster is initialized. It gives the
 * application an opportunity to take care of cluster initialization procedures.
 * It is called exactly once for each endpoint where cluster is present.
 *
 * @param endpoint   Ver.: always
 *
 * TODO Issue #3841
 * emberAfOnOffClusterInitCallback happens before the stack initialize the cluster
 * attributes to the default value.
 * The logic here expects something similar to the deprecated Plugins callback
 * emberAfPluginOnOffClusterServerPostInitCallback.
 *
 */
void emberAfOnOffClusterInitCallback(EndpointId endpoint) {
  EmberAfStatus status;
  bool storedValue;

  /* Read storedValue on/off value */
  status = OnOff::Attributes::OnOff::Get(endpoint, &storedValue);
  if (status == EMBER_ZCL_STATUS_SUCCESS) {
    /* Set actual state to the cluster state that was last persisted */
    AppEvent event;
    event.Type = AppEventType::Lighting;
    event.LightingEvent.Action = LightingAction::SetEnable;
    event.LightingEvent.Value = storedValue;
    AppTask::Instance().ZCLHandler(event);
  }
  AppTask::Instance().UpdateClusterState();
}

void emberAfColorControlClusterInitCallback(EndpointId endpoint) {
  EmberAfStatus status;
  uint8_t storedHue;
  uint8_t storedSaturation;

  status = ColorControl::Attributes::CurrentHue::Get(endpoint, &storedHue);
  if (status != EMBER_ZCL_STATUS_SUCCESS) {
    status = ColorControl::Attributes::CurrentSaturation::Get(endpoint, &storedSaturation);
    if (status == EMBER_ZCL_STATUS_SUCCESS) {
      // Set actual state to the cluster state that was last persisted
      AppEvent event;
      event.Type = AppEventType::Lighting;
      event.LightingEvent.Action = LightingAction::SetHueSaturation;
      event.LightingEvent.Value = storedHue;
      event.LightingEvent.Value2 = storedSaturation;
      event.LightingEvent.isRelative = false;
      AppTask::Instance().ZCLHandler(event);
      ChipLogProgress(Zcl, "emberAfOnOffClusterInitCallback: restore the hue %d and saturation %d",
                      storedHue, storedSaturation);
    }
  }
}