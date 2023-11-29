/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#pragma once

#include <cstdint>

class LEDWidget;

enum class LightingAction : uint8_t {
  None = 0,
  SetLevel,
  SetEnable,
  SetHue,
  SetSaturation,
  SetHueSaturation,
  Blink,
};

enum class AppEventType : uint8_t {
  None = 0,
  Button,
  ButtonPushed,
  ButtonReleased,
  Timer,
  UpdateLedState,
  Lighting,
  IdentifyStart,
  IdentifyStop,
};

enum class FunctionEvent : uint8_t { NoneSelected = 0, FactoryReset };

struct AppEvent;
using EventHandler = void (*)(const AppEvent &);

struct AppEvent {
  union {
    struct {
      uint8_t PinNo;
      uint8_t Action;
    } ButtonEvent;
    struct {
      void *Context;
    } TimerEvent;
    struct {
      LightingAction Action;
      uint32_t Value;
      uint8_t Value2;
      bool isRelative;
    } LightingEvent;
    struct {
      LEDWidget *LedWidget;
    } UpdateLedStateEvent;
  };

  AppEventType Type{AppEventType::None};
  EventHandler Handler;
};
