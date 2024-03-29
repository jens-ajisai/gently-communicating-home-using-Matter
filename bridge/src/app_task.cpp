/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "app_task.h"

#include <app/server/OnboardingCodesUtil.h>
#include <app/server/Server.h>
#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/CodeUtils.h>
#include <platform/CHIPDeviceLayer.h>
#include <system/SystemError.h>

#include "app_config.h"
#include "bridge/bridge_manager.h"
#include "bridge/matter_device_ble.h"
#include "bridge/matter_device_fixed.h"
#include "bridge/oob_exchange_manager.h"
#include "fabric_table_delegate.h"
#include "uart/nfc_uart.h"
#include "display.h"

#include "reminders/reminders_app.h"

#if defined(CONFIG_WIFI)
#include <date_time.h>
#endif

#ifdef CONFIG_CHIP_WIFI
#include <app/clusters/network-commissioning/network-commissioning.h>
#include <platform/nrfconnect/wifi/NrfWiFiDriver.h>
#endif

#ifdef CONFIG_CHIP_OTA_REQUESTOR
#include "ota_util.h"
#endif

#include <dk_buttons_and_leds.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace ::chip;
using namespace ::chip::app;
using namespace ::chip::Credentials;
using namespace ::chip::DeviceLayer;

namespace {
constexpr uint32_t kFactoryResetTriggerTimeout = 6000;
constexpr size_t kAppEventQueueSize = 10;

K_MSGQ_DEFINE(sAppEventQueue, sizeof(AppEvent), kAppEventQueueSize, alignof(AppEvent));
k_timer sFunctionTimer;
k_timer sBridgeStartTimer;

LEDWidget sStatusLED;

bool sIsNetworkProvisioned = false;
bool sIsNetworkEnabled = false;
bool sHaveBLEConnections = false;

} /* namespace */

namespace LedConsts {
namespace StatusLed {
namespace Unprovisioned {
constexpr uint32_t kOn_ms{100};
constexpr uint32_t kOff_ms{kOn_ms};
}  // namespace Unprovisioned
namespace Provisioned {
constexpr uint32_t kOn_ms{50};
constexpr uint32_t kOff_ms{950};
}  // namespace Provisioned

}  // namespace StatusLed

namespace FactoryLed {
constexpr uint32_t kBlinkRate_ms{100};
}  // namespace FactoryLed

}  // namespace LedConsts

#ifdef CONFIG_CHIP_WIFI
app::Clusters::NetworkCommissioning::Instance sWiFiCommissioningInstance(
    0, &(NetworkCommissioning::NrfWiFiDriver::Instance()));
#endif

#include "bridge/config.inc"


// Handle repeated card id events
static volatile bool skipRepeat = false;
static char lastId[20];
struct k_work_delayable cardRepeatTimerWork;

static void onCardRepeatTimer(struct k_work *work) {
  skipRepeat = false;
}


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

  if (IS_ENABLED(CONFIG_SETTINGS)) {
    int err2 = settings_load();
    if (err2) {
      LOG_ERR("Failed to load settings (err %d)", err2);
    } else {
      LOG_INF("Loaded settings (err %d)", err2);
    }
  }

#if defined(CONFIG_NET_L2_OPENTHREAD)
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
#elif defined(CONFIG_CHIP_WIFI)
  sWiFiCommissioningInstance.Init();
#else
  return CHIP_ERROR_INTERNAL;
#endif /* CONFIG_NET_L2_OPENTHREAD */
  /* Initialize LEDs */
  LEDWidget::InitGpio();
  LEDWidget::SetStateUpdateCallback(LEDStateUpdateHandler);

  sStatusLED.Init(SYSTEM_STATE_LED);

  UpdateStatusLED();

  /* Initialize buttons */
  int ret = dk_buttons_init(ButtonEventHandler);
  if (ret) {
    LOG_ERR("dk_buttons_init() failed");
    return chip::System::MapErrorZephyr(ret);
  }

  /* Initialize function timer */
  k_timer_init(&sFunctionTimer, &AppTask::FunctionTimerTimeoutCallback, nullptr);
  k_timer_user_data_set(&sFunctionTimer, this);

  k_timer_init(&sBridgeStartTimer, &AppTask::BridgeStartCallback, nullptr);
  k_timer_user_data_set(&sBridgeStartTimer, this);

#ifdef CONFIG_CHIP_OTA_REQUESTOR
  /* OTA image confirmation must be done before the factory data init. */
  OtaConfirmNewImage();
#endif

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

  ConfigurationMgr().LogDeviceConfig();
  PrintOnboardingCodes(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE));
  AppFabricTableDelegate::Init();

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

  NfcUart::Instance().Init(UartMessageHandler);

  display_init();
  k_work_init_delayable(&cardRepeatTimerWork, onCardRepeatTimer);

  return CHIP_NO_ERROR;
}

CHIP_ERROR AppTask::StartApp() {
  ReturnErrorOnFailure(Init());

  AppEvent event = {};

  while (true) {
    k_msgq_get(&sAppEventQueue, &event, K_FOREVER);
    DispatchEvent(event);
  }

  return CHIP_NO_ERROR;
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
  } else if (RECORD_BUTTON_MASK & hasChanged) {
    LOG_INF("ButtonEventHandler Post Button event RECORD_BUTTON");
    button_event.ButtonEvent.PinNo = RECORD_BUTTON;
    button_event.ButtonEvent.Action =
        static_cast<uint8_t>((RECORD_BUTTON_MASK & buttonState) ? AppEventType::ButtonPushed
                                                                : AppEventType::ButtonReleased);
    button_event.Handler = FunctionHandler;
    PostEvent(button_event);
  }
}

void AppTask::BridgeStartCallback(k_timer *timer) {
  chip::DeviceLayer::PlatformMgr().ScheduleWork(
      [](intptr_t context) {
        initRemindersApp(display_updateTranscription, display_updateCompletions);
        InitBridge();
      },
      reinterpret_cast<intptr_t>(nullptr));  
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

void AppTask::FunctionTimerEventHandler(const AppEvent &) {
  if (Instance().mFunction == FunctionEvent::FactoryReset) {
    Instance().mFunction = FunctionEvent::NoneSelected;
    LOG_INF("Factory Reset triggered");

    sStatusLED.Blink(LedConsts::FactoryLed::kBlinkRate_ms);

    chip::Server::GetInstance().ScheduleFactoryReset();
  }
}

void AppTask::FunctionHandler(const AppEvent &event) {
  if (event.ButtonEvent.PinNo == FUNCTION_BUTTON) {
    if (event.ButtonEvent.Action == static_cast<uint8_t>(AppEventType::ButtonPushed)) {
      Instance().StartTimer(kFactoryResetTriggerTimeout);
      Instance().mFunction = FunctionEvent::FactoryReset;
    } else if (event.ButtonEvent.Action == static_cast<uint8_t>(AppEventType::ButtonReleased)) {
      if (Instance().mFunction == FunctionEvent::FactoryReset) {
        UpdateStatusLED();
        Instance().CancelTimer();
        Instance().mFunction = FunctionEvent::NoneSelected;
        LOG_INF("Factory Reset has been Canceled. Init Bridge.");
      }
    }
  } else if (event.ButtonEvent.PinNo == RECORD_BUTTON) {
    if (event.ButtonEvent.Action == static_cast<uint8_t>(AppEventType::ButtonPushed)) {
      startAiFlow();
    } else if (event.ButtonEvent.Action == static_cast<uint8_t>(AppEventType::ButtonReleased)) {
      stopRecording();
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
  if (sIsNetworkProvisioned && sIsNetworkEnabled) {
    sStatusLED.Set(true);
  } else if (sHaveBLEConnections) {
    sStatusLED.Blink(LedConsts::StatusLed::Unprovisioned::kOn_ms,
                     LedConsts::StatusLed::Unprovisioned::kOff_ms);
  } else {
    sStatusLED.Blink(LedConsts::StatusLed::Provisioned::kOn_ms,
                     LedConsts::StatusLed::Provisioned::kOff_ms);
  }
}

void AppTask::ChipEventHandler(const ChipDeviceEvent *event, intptr_t /* arg */) {
  switch (event->Type) {
    case DeviceEventType::kCHIPoBLEAdvertisingChange:
      sHaveBLEConnections = ConnectivityMgr().NumBLEConnections() != 0;
      UpdateStatusLED();
      break;
#if defined(CONFIG_NET_L2_OPENTHREAD)
    case DeviceEventType::kDnssdInitialized:
#if CONFIG_CHIP_OTA_REQUESTOR
      InitBasicOTARequestor();
#endif
      break;
    case DeviceEventType::kThreadStateChange:
      sIsNetworkProvisioned = ConnectivityMgr().IsThreadProvisioned();
      sIsNetworkEnabled = ConnectivityMgr().IsThreadEnabled();
#elif defined(CONFIG_CHIP_WIFI)
    case DeviceEventType::kWiFiConnectivityChange:
      sIsNetworkProvisioned = ConnectivityMgr().IsWiFiStationProvisioned();
      sIsNetworkEnabled = ConnectivityMgr().IsWiFiStationEnabled();
#if CONFIG_CHIP_OTA_REQUESTOR
      if (event->WiFiConnectivityChange.Result == kConnectivity_Established) {
        InitBasicOTARequestor();
      }
#endif /* CONFIG_CHIP_OTA_REQUESTOR */
#endif
      UpdateStatusLED();
      k_timer_start(&sBridgeStartTimer, K_SECONDS(10), K_NO_WAIT);
      break;
    default:
      break;
  }
}

void AppTask::UartMessageHandler(char *buf, uint16_t len) {
  AppEvent event;
  event.Type = AppEventType::UartMessage;
  event.Handler = ParseUartMessageHandler;
  if (len > sizeof(event.UartEvent.msg)) {
    LOG_INF("UART message larger than buffer %d > %d:", len, sizeof(event.UartEvent.msg));
    memcpy(event.UartEvent.msg, buf, sizeof(event.UartEvent.msg));
    event.UartEvent.msg[sizeof(event.UartEvent.msg) - 1] = 0;
  } else {
    memcpy(event.UartEvent.msg, buf, len);
  }
  PostEvent(event);
}

void AppTask::ParseUartMessageHandler(const AppEvent &event) {
  if (event.Type == AppEventType::UartMessage) {
    if (strncmp(event.UartEvent.msg, "ID:", 3) == 0) {
      //  check if the id is the same as the last one
      if(strncmp(lastId, event.UartEvent.msg, sizeof(lastId)) == 0) {
        if(skipRepeat) return;
      }
      
      // save the id to compare it to the next one
      strncpy(lastId, event.UartEvent.msg, sizeof(lastId));
      skipRepeat = true;
      k_work_reschedule(&cardRepeatTimerWork, K_SECONDS(5)); 

      // Card Id event
      LOG_INF("Received card id event: %s", event.UartEvent.msg);
      cardTriggerAction(event.UartEvent.msg);
    } else {
      // OOB event
      OobExchangeManager::Instance().ExchangeOob((char *)event.UartEvent.msg, NfcUart::SendEntry);
    }
  }
}

void AppTask::CancelTimer() { k_timer_stop(&sFunctionTimer); }

void AppTask::StartTimer(uint32_t timeoutInMs) {
  k_timer_start(&sFunctionTimer, K_MSEC(timeoutInMs), K_NO_WAIT);
}

void AppTask::PostEvent(const AppEvent &event) {
  if (k_msgq_put(&sAppEventQueue, &event, K_NO_WAIT) != 0) {
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

void AppTask::InitBridge() {

  chip::DeviceLayer::PlatformMgr().ScheduleWork(
      [](intptr_t context) {
        struct MatterDeviceBle::MatterDeviceConfiguration conf;
        conf.filter = deviceFilter;
        conf.matterBleMapping = matterBleMapping;
        conf.ep = &bridgedPostureEndpoint;
        conf.deviceTypes = &postureDeviceTypesSpan;
        conf.dataVersions = &postureDataVersionsSpan;
        strncpy(conf.name, "posture", 8);

        // By coincidence the reminders use the same device type and clusters
        // Try to reuse all the definitions except the name
        struct MatterDeviceFixed::MatterDeviceConfiguration conf2;
        conf2.ep = &bridgedPostureEndpoint;
        conf2.deviceTypes = &postureDeviceTypesSpan;
        conf2.dataVersions = &postureDataVersionsSpan;
        strncpy(conf2.name, "reminder", 9);


        /* Initialize bridge manager */
        CHIP_ERROR err = BridgeManager::Instance().Init(conf, conf2);
        if (err != CHIP_NO_ERROR) {
          LOG_ERR("BridgeManager initialization failed");
        } else {
          LOG_INF("BridgeManager initialization ok");
        }
      },
      reinterpret_cast<intptr_t>(nullptr));
}