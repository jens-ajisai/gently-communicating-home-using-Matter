#include "matter_device_fixed.h"

#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/util/af-types.h>
#include <lib/core/DataModelTypes.h>
#include <platform/PlatformManager.h>
#include <zephyr/logging/log.h>

#include "../reminders/reminders_app.h"
#include "bridge_manager.h"
LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace ::chip::app::Clusters;

#define REMINDER_CHECK_INTERVAL (60)

struct work_with_data {
  struct k_work_delayable work;
  MatterDeviceFixed *dev;
};

static struct work_with_data reminderCheckWork;

static uint16_t convertToLevel(int64_t remaining) {
  // 3 hours (10800s) and more left is level 0
  // deadline passed is level (0s) is level 255
  int max = 10800;
  int min = 0;
  int max_target = 255;

  // Clip the remaining time by min and max
  int32_t remainingClipped = MAX(min, MIN(remaining, max));
  // transfer to new scale (simplified since both min are 0)
  uint16_t target = (1.0 * max_target / max) * remainingClipped;
  // invert the scale
  target = max_target - target;
  return target;
}

static void onReminderCheckEntry(struct k_work *work) {
  struct work_with_data *work_data = CONTAINER_OF(work, struct work_with_data, work);
  work_data->dev->onReminderCheck();
}

void MatterDeviceFixed::onReminderCheck() {
  LOG_INF("MatterDeviceFixed::onReminderCheck");

  auto path = chip::Platform::New<chip::app::ConcreteAttributePath>(
      GetEndpointId(), LevelControl::Id, LevelControl::Attributes::CurrentLevel::Id);

  int64_t remaining = checkdue();
  if (remaining != LONG_MAX) {
    uint16_t value = convertToLevel(remaining);

    LOG_INF("MatterDeviceFixed::onReminderCheck set value to %d", value);
    attributeCache[LevelControl::Attributes::CurrentLevel::Id] = value;
    chip::DeviceLayer::PlatformMgr().ScheduleWork(
        [](intptr_t context) {
          auto path = reinterpret_cast<chip::app::ConcreteAttributePath *>(context);
          BridgeManager::Instance().HandleUpdate(path);
        },
        reinterpret_cast<intptr_t>(path));
    if (!GetIsReachable()) SetIsReachable(true);
  } else {
    if (GetIsReachable()) SetIsReachable(false);
  }
  k_work_reschedule(&reminderCheckWork.work, K_SECONDS(REMINDER_CHECK_INTERVAL));
}

/****************************
 * Init
 ****************************/
void MatterDeviceFixed::Init() {
  LOG_INF("MatterDeviceFixed::Init");

  reminderCheckWork.dev = this;
  k_work_init_delayable(&reminderCheckWork.work, onReminderCheckEntry);
  k_work_reschedule(&reminderCheckWork.work, K_NO_WAIT);

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
