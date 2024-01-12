/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#pragma once

#include <cstdint>

// only for OobExchangeManager::oobMessageLen
#include "../bridge/oob_exchange_manager.h"

class LEDWidget;

enum class AppEventType : uint8_t {
  None = 0,
  Button,
  ButtonPushed,
  ButtonReleased,
  Timer,
  UpdateLedState,
  UartMessage
};

enum class FunctionEvent : uint8_t { 
	NoneSelected = 0,
	FactoryReset
};

struct AppEvent;
using EventHandler = void (*)(const AppEvent &);

struct AppEvent {
  union {
    struct {
      char msg[OobExchangeManager::oobMessageLen];
    } UartEvent;
    struct {
      uint8_t PinNo;
      uint8_t Action;
    } ButtonEvent;
    struct {
      void *Context;
    } TimerEvent;
    struct {
      LEDWidget *LedWidget;
    } UpdateLedStateEvent;
  };

  AppEventType Type{AppEventType::None};
  EventHandler Handler;
};
