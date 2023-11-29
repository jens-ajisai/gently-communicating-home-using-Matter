/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "app_task.h"

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app/DeferredAttributePersistenceProvider.h>
#include <app/clusters/identify-server/identify-server.h>
#include <app/server/OnboardingCodesUtil.h>
#include <app/server/Server.h>
#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <dk_buttons_and_leds.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/CodeUtils.h>
#include <platform/CHIPDeviceLayer.h>
#include <system/SystemError.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_config.h"
#include "led_strip.h"
#include "led_widget.h"

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace ::chip;
using namespace ::chip::app;
using namespace ::chip::Credentials;
using namespace ::chip::DeviceLayer;

namespace {
constexpr uint32_t factoryResetTriggerTimeout = 6000;
constexpr size_t appEventQueueSize = 10;
constexpr EndpointId lightEndpointId = 1;
constexpr uint8_t defaultMinLevel = 0;
constexpr uint8_t defaultMaxLevel = 254;

K_MSGQ_DEFINE(appEventQueue, sizeof(AppEvent), appEventQueueSize, alignof(AppEvent));
k_timer functionTimer;

Identify identify = {lightEndpointId, AppTask::IdentifyStartHandler, AppTask::IdentifyStopHandler,
                     EMBER_ZCL_IDENTIFY_IDENTIFY_TYPE_VISIBLE_LED, NULL};

LEDWidget statusLED;
LEDWidget factoryResetLED;
LedStrip light;

bool isNetworkProvisioned = false;
bool isNetworkEnabled = false;
bool haveBLEConnections = false;

// Define a custom attribute persister which makes actual write of the CurrentLevel attribute value
// to the non-volatile storage only when it has remained constant for 5 seconds. This is to reduce
// the flash wearout when the attribute changes frequently as a result of MoveToLevel command.
// DeferredAttribute object describes a deferred attribute, but also holds a buffer with a value to
// be written, so it must live so long as the DeferredAttributePersistenceProvider object.

DeferredAttribute deferredAttributes[] = {
    DeferredAttribute(ConcreteAttributePath(lightEndpointId, Clusters::LevelControl::Id,
                                            Clusters::LevelControl::Attributes::CurrentLevel::Id)),
    DeferredAttribute(ConcreteAttributePath(lightEndpointId, Clusters::ColorControl::Id,
                                            Clusters::ColorControl::Attributes::CurrentHue::Id)),
    DeferredAttribute(ConcreteAttributePath(lightEndpointId, Clusters::ColorControl::Id,
                              Clusters::ColorControl::Attributes::CurrentSaturation::Id)),
};

DeferredAttributePersistenceProvider deferredAttributePersister(
    Server::GetInstance().GetDefaultAttributePersister(),
    Span<DeferredAttribute>(deferredAttributes, 3), System::Clock::Milliseconds32(5000));

} /* namespace */

namespace LedConsts {
constexpr uint32_t identifyBlinkRate_ms{500};

namespace StatusLed {
namespace Unprovisioned {
constexpr uint32_t on_ms{100};
constexpr uint32_t off_ms{on_ms};
}  // namespace Unprovisioned
namespace Provisioned {
constexpr uint32_t on_ms{50};
constexpr uint32_t off_ms{950};
}  // namespace Provisioned

}  // namespace StatusLed

}  // namespace LedConsts

CHIP_ERROR AppTask::Init() {
  /* Initialize CHIP stack */
  LOG_INF("Init CHIP stack");

  CHIP_ERROR err = chip::Platform::MemoryInit();
  if (err != CHIP_NO_ERROR) {
    LOG_ERR("Platform::MemoryInit() failed");
    return err;
  }

  err = PlatformMgr().InitChipStack();
  if (err != CHIP_NO_ERROR) {
    LOG_ERR("PlatformMgr().InitChipStack() failed");
    return err;
  }

  err = ThreadStackMgr().InitThreadStack();
  if (err != CHIP_NO_ERROR) {
    LOG_ERR("ThreadStackMgr().InitThreadStack() failed: %s", ErrorStr(err));
    return err;
  }

  err = ConnectivityMgr().SetThreadDeviceType(ConnectivityManager::kThreadDeviceType_Router);

  if (err != CHIP_NO_ERROR) {
    LOG_ERR("ThreadStackMgr().InitThreadStack() failed: %s", ErrorStr(err));
    return err;
  }

  /* Initialize LEDs */
  LEDWidget::InitGpio();
  LEDWidget::SetStateUpdateCallback(LEDStateUpdateHandler);

  statusLED.Init(SYSTEM_STATE_LED);
  factoryResetLED.Init(FACTORY_RESET_SIGNAL_LED);

  UpdateStatusLED();

  /* Initialize buttons */
  int ret = dk_buttons_init(ButtonEventHandler);
  if (ret) {
    LOG_ERR("dk_buttons_init() failed");
    return chip::System::MapErrorZephyr(ret);
  }

  /* Initialize function timer */
  k_timer_init(&functionTimer, &AppTask::FunctionTimerTimeoutCallback, nullptr);
  k_timer_user_data_set(&functionTimer, this);

  /* Initialize lighting device (PWM) */
  uint8_t minLightLevel = defaultMinLevel;
  Clusters::LevelControl::Attributes::MinLevel::Get(lightEndpointId, &minLightLevel);

  uint8_t maxLightLevel = defaultMaxLevel;
  Clusters::LevelControl::Attributes::MaxLevel::Get(lightEndpointId, &maxLightLevel);

  light.init(defaultMinLevel, defaultMaxLevel);

  /* Initialize CHIP server */
#if CONFIG_CHIP_FACTORY_DATA
  ReturnErrorOnFailure(mFactoryDataProvider.Init());
  SetDeviceInstanceInfoProvider(&mFactoryDataProvider);
  SetDeviceAttestationCredentialsProvider(&mFactoryDataProvider);
  SetCommissionableDataProvider(&mFactoryDataProvider);
#else
  SetDeviceInstanceInfoProvider(&DeviceInstanceInfoProviderMgrImpl());
  SetDeviceAttestationCredentialsProvider(Examples::GetExampleDACProvider());
#endif

  static chip::CommonCaseDeviceServerInitParams initParams;
  (void)initParams.InitializeStaticResourcesBeforeServerInit();

  ReturnErrorOnFailure(chip::Server::GetInstance().Init(initParams));
  app::SetAttributePersistenceProvider(&deferredAttributePersister);

  ConfigurationMgr().LogDeviceConfig();
  PrintOnboardingCodes(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE));

  /*
   * Add CHIP event handler and start CHIP thread.
   * Note that all the initialization code should happen prior to this point to avoid data races
   * between the main and the CHIP threads.
   */
  PlatformMgr().AddEventHandler(ChipEventHandler, 0);

  err = PlatformMgr().StartEventLoopTask();
  if (err != CHIP_NO_ERROR) {
    LOG_ERR("PlatformMgr().StartEventLoopTask() failed");
    return err;
  }

  return CHIP_NO_ERROR;
}

CHIP_ERROR AppTask::StartApp() {
  ReturnErrorOnFailure(Init());

  AppEvent event = {};

  while (true) {
    k_msgq_get(&appEventQueue, &event, K_FOREVER);
    DispatchEvent(event);
  }

  return CHIP_NO_ERROR;
}

void AppTask::IdentifyStartHandler(Identify *) {
  LOG_INF("Start identifying");
  AppEvent event;
  event.Type = AppEventType::IdentifyStart;
  event.Handler = [](const AppEvent &) {
    light.blink(LedConsts::identifyBlinkRate_ms, LedConsts::identifyBlinkRate_ms);
  };
  PostEvent(event);
}

void AppTask::IdentifyStopHandler(Identify *) {
  LOG_INF("Stop identifying");
  AppEvent event;
  event.Type = AppEventType::IdentifyStop;
  event.Handler = [](const AppEvent &) { light.blink(0, 0); };
  PostEvent(event);
}

void AppTask::ZCLHandler(const AppEvent &event) {
  LOG_INF("ZCLHandler with AppEvent type %d, action %d, value %d", (uint8_t)event.Type,
          (uint8_t)event.LightingEvent.Action, event.LightingEvent.Value);
  AppEvent ev(event);
  ev.Handler = LightingActionEventHandler;
  PostEvent(ev);
}

void AppTask::LightingActionEventHandler(const AppEvent &event) {
  LOG_INF("LightingActionEventHandler with AppEvent type %d, action %d, value %d",
          (uint8_t)event.Type, (uint8_t)event.LightingEvent.Action, event.LightingEvent.Value);
  if (event.Type == AppEventType::Button) {
    light.isTurnedOn() ? light.enable(false) : light.enable(true);
    Instance().UpdateClusterState();
  } else if (event.Type == AppEventType::Lighting) {
    switch (event.LightingEvent.Action) {
      case LightingAction::SetEnable:
        light.enable(event.LightingEvent.Value);
        break;
      case LightingAction::SetLevel:
        light.setLevel(event.LightingEvent.Value);
        break;
      case LightingAction::SetHue:
        light.setHue(event.LightingEvent.Value, event.LightingEvent.isRelative);
        break;
      case LightingAction::SetSaturation:
        light.setSaturation(event.LightingEvent.Value, event.LightingEvent.isRelative);
        break;
      case LightingAction::SetHueSaturation:
        light.setHue(event.LightingEvent.Value, event.LightingEvent.isRelative);
        light.setSaturation(event.LightingEvent.Value2, event.LightingEvent.isRelative);
        break;
      default:
        break;
    }
  }
}

void AppTask::ButtonEventHandler(uint32_t buttonState, uint32_t hasChanged) {
  AppEvent button_event;
  button_event.Type = AppEventType::Button;

  if (FUNCTION_BUTTON_MASK & hasChanged) {
    button_event.ButtonEvent.PinNo = FUNCTION_BUTTON;
    button_event.ButtonEvent.Action =
        static_cast<uint8_t>((FUNCTION_BUTTON_MASK & buttonState) ? AppEventType::ButtonPushed
                                                                  : AppEventType::ButtonReleased);
    button_event.Handler = FunctionHandler;
    PostEvent(button_event);
  }
}

void AppTask::FunctionTimerTimeoutCallback(k_timer *timer) {
  if (!timer) {
    return;
  }

  AppEvent event;
  event.Type = AppEventType::Timer;
  event.TimerEvent.Context = k_timer_user_data_get(timer);
  event.Handler = FunctionTimerEventHandler;
  PostEvent(event);
}

void AppTask::FunctionTimerEventHandler(const AppEvent &event) {
  if (event.Type != AppEventType::Timer) {
    return;
  }

  if (Instance().mFunction == FunctionEvent::FactoryReset) {
    Instance().mFunction = FunctionEvent::NoneSelected;
    LOG_INF("Factory Reset triggered");

    statusLED.Set(true);
    factoryResetLED.Set(true);

    chip::Server::GetInstance().ScheduleFactoryReset();
  }
}

void AppTask::FunctionHandler(const AppEvent &event) {
  if (event.ButtonEvent.PinNo != FUNCTION_BUTTON) return;

  if (event.ButtonEvent.Action == static_cast<uint8_t>(AppEventType::ButtonPushed)) {
    Instance().StartTimer(factoryResetTriggerTimeout);
    Instance().mFunction = FunctionEvent::FactoryReset;
  } else if (event.ButtonEvent.Action == static_cast<uint8_t>(AppEventType::ButtonReleased)) {
    if (Instance().mFunction == FunctionEvent::FactoryReset) {
      factoryResetLED.Set(false);
      UpdateStatusLED();
      Instance().CancelTimer();
      Instance().mFunction = FunctionEvent::NoneSelected;
      LOG_INF("Factory Reset has been Canceled. Toggle Light.");

      AppEvent button_event;
      button_event.Type = AppEventType::Button;
      button_event.Handler = LightingActionEventHandler;
      PostEvent(button_event);
    }
  }
}

void AppTask::LEDStateUpdateHandler(LEDWidget &ledWidget) {
  AppEvent event;
  event.Type = AppEventType::UpdateLedState;
  event.Handler = UpdateLedStateEventHandler;
  event.UpdateLedStateEvent.LedWidget = &ledWidget;
  PostEvent(event);
}

void AppTask::UpdateLedStateEventHandler(const AppEvent &event) {
  if (event.Type == AppEventType::UpdateLedState) {
    event.UpdateLedStateEvent.LedWidget->UpdateState();
  }
}

void AppTask::UpdateStatusLED() {
  /* Update the status LED.
   *
   * If IPv6 networking and service provisioned, keep the LED On constantly.
   *
   * If the system has BLE connection(s) uptill the stage above, THEN blink the LED at an even
   * rate of 100ms.
   *
   * Otherwise, blink the LED for a very short time. */
  if (isNetworkProvisioned && isNetworkEnabled) {
    statusLED.Set(true);
  } else if (haveBLEConnections) {
    statusLED.Blink(LedConsts::StatusLed::Unprovisioned::on_ms,
                    LedConsts::StatusLed::Unprovisioned::off_ms);
  } else {
    statusLED.Blink(LedConsts::StatusLed::Provisioned::on_ms,
                    LedConsts::StatusLed::Provisioned::off_ms);
  }
}

void AppTask::ChipEventHandler(const ChipDeviceEvent *event, intptr_t /* arg */) {
  switch (event->Type) {
    case DeviceEventType::kCHIPoBLEAdvertisingChange:
      haveBLEConnections = ConnectivityMgr().NumBLEConnections() != 0;
      UpdateStatusLED();
      break;
    case DeviceEventType::kDnssdInitialized:
      break;
    case DeviceEventType::kThreadStateChange:
      isNetworkProvisioned = ConnectivityMgr().IsThreadProvisioned();
      isNetworkEnabled = ConnectivityMgr().IsThreadEnabled();
      UpdateStatusLED();
      break;
    default:
      break;
  }
}

void AppTask::CancelTimer() { k_timer_stop(&functionTimer); }

void AppTask::StartTimer(uint32_t timeoutInMs) {
  k_timer_start(&functionTimer, K_MSEC(timeoutInMs), K_NO_WAIT);
}

void AppTask::PostEvent(const AppEvent &event) {
  if (k_msgq_put(&appEventQueue, &event, K_NO_WAIT) != 0) {
    LOG_INF("Failed to post event to app task event queue");
  }
}

void AppTask::DispatchEvent(const AppEvent &event) {
  if (event.Handler) {
    event.Handler(event);
  } else {
    LOG_INF("Event received with no handler. Dropping event.");
  }
}

void AppTask::UpdateClusterState() {
  SystemLayer().ScheduleLambda([this] {
    // write the new on/off value
    EmberAfStatus status =
        Clusters::OnOff::Attributes::OnOff::Set(lightEndpointId, light.isTurnedOn());

    if (status != EMBER_ZCL_STATUS_SUCCESS) {
      LOG_ERR("Updating on/off cluster failed: %x", status);
    }

    // write the current level
    status =
        Clusters::LevelControl::Attributes::CurrentLevel::Set(lightEndpointId, light.getLevel());

    if (status != EMBER_ZCL_STATUS_SUCCESS) {
      LOG_ERR("Updating level cluster failed: %x", status);
    }

    // write the color hue
    status = Clusters::ColorControl::Attributes::CurrentHue::Set(lightEndpointId, light.getHue());

    if (status != EMBER_ZCL_STATUS_SUCCESS) {
      LOG_ERR("Updating level cluster failed: %x", status);
    }

    // write the color saturation
    status = Clusters::ColorControl::Attributes::CurrentSaturation::Set(lightEndpointId,
                                                                        light.getSaturation());

    if (status != EMBER_ZCL_STATUS_SUCCESS) {
      LOG_ERR("Updating level cluster failed: %x", status);
    }

    LOG_INF("Updated cluster state endpoint %d to : %d level %d", lightEndpointId,
            light.isTurnedOn(), light.getLevel());
  });
}
