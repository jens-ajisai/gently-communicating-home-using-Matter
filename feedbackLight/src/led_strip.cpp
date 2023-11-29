#include "led_strip.h"

#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

#include <algorithm>
#include <cmath>

// Following two functions by alexkuhl colorspace-conversion-library
//   from https://github.com/alexkuhl/colorspace-conversion-library

/*
Copyright (c) 2010, Alex Kuhl, http://www.alexkuhl.org
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of the author nor the names of other
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/* A small, easy-to-understand C++ library written to convert colorspaces
 * Will convert RGB to HSV or HSL and back to RGB
 * Note on the colors:
 * R,G,B are ints [0,255]
 * S,V,L are floats [0,1]
 * H is a float [0,360)
 *
 * Requires the CImg library header file (testing code used version 1.3.1)
 * http://cimg.sourceforge.net/
 */

// -- start
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnarrowing"
void hsl_to_rgb(float h, float s, float l, int &r, int &g, int &b) {
  float q;
  if (l < .5)
    q = l * (1 + s);
  else
    q = l + s - (l * s);
  float p = 2 * l - q;
  h /= 360;
  // t holds r, g, and b values
  float t[3] = {h + 1 / 3.0, h, h - 1 / 3.0};
  for (int i = 0; i < 3; ++i) {
    if (t[i] < 0)
      t[i] += 1;
    else if (t[i] > 1)
      t[i] -= 1;
    // calculate the color
    if (t[i] < 1.0 / 6.0)
      t[i] = p + ((q - p) * 6 * t[i]);
    else if (1.0 / 6.0 <= t[i] && t[i] < .5)
      t[i] = q;
    else if (.5 <= t[i] && t[i] < 2.0 / 3.0)
      t[i] = p + ((q - p) * 6 * (2.0 / 3.0 - t[i]));
    else
      t[i] = p;
  }
  r = round(t[0] * 255);
  g = round(t[1] * 255);
  b = round(t[2] * 255);
}

void rgb_to_hsl(int r, int g, int b, float &h, float &s, float &l) {
  float fr = r / 255.0, fg = g / 255.0, fb = b / 255.0;
  int imax = std::max(std::max(r, g), b);
  int imin = std::min(std::min(r, g), b);
  float fmax = imax / 255.0;
  float fmin = imin / 255.0;
  float multiplier = (imin == imax) ? 0.0 : 60 / (fmax - fmin);

  if (r == imax)  // red is dominant
  {
    h = (multiplier * (fg - fb) + 360);
    if (h >= 360) h -= 360;  // take quick modulus, % doesn't work with floats
  } else if (g == imax)      // green is dominant
    h = multiplier * (fb - fr) + 120;
  else  // blue is dominant
    h = multiplier * (fr - fg) + 240;
  l = .5 * (fmax + fmin);
  if (imax == imin)
    s = 0.0;
  else if (l <= .5)
    s = (fmax - fmin) / (2 * l);
  else
    s = (fmax - fmin) / (2 - 2 * l);
}
#pragma GCC diagnostic pop
// -- end

void LedStrip::init(uint8_t minLevel, uint8_t maxLevel) {
  if (!device_is_ready(strip)) {
    LOG_ERR("LED strip device %s is not ready", strip->name);
  }

  mMinLevel = minLevel;
  mMaxLevel = maxLevel;

  // mColorHsl is active on startup
  mColorRgb = COLOR_RGB_WHITE;
  mColorHsl = COLOR_HSL_WHITE;

  mState = stateOff;
  mBlinkOnTimeMS = 0;
  mBlinkOffTimeMS = 0;

  k_timer_init(&mLedTimer, &LedStrip::ledStateTimerHandlerEntry, nullptr);
  k_timer_user_data_set(&mLedTimer, this);

  LOG_INF("Init LED strip device %s level min-max (%d-%d) to color GREEN state off", strip->name,
          minLevel, maxLevel);
  apply();
}

void LedStrip::enable(bool on) {
  LOG_INF("LED strip enable %s", on ? "on" : "off");

  mState = on ? stateOn : stateOff;
  mBlinkOnTimeMS = 0;
  mBlinkOffTimeMS = 0;

  apply();
}

void LedStrip::setLevel(uint8_t level) {
  LOG_INF("LED strip set level to %d", level);

  if (level < mMinLevel) level = mMinLevel;
  if (level > mMaxLevel) level = mMaxLevel;

  mColorHsl.l = (float)level / (float)mMaxLevel;
  mBlinkOnTimeMS = 0;
  mBlinkOffTimeMS = 0;

  apply();
}

void LedStrip::setColor(uint32_t rgba) {
  uint8_t *p = (uint8_t *)&rgba;
  setColor(p[0], p[1], p[2]);
}

void LedStrip::setColor(uint8_t r, uint8_t g, uint8_t b) {
  LOG_INF("LED strip set color to (%d,%d,%d)", r, g, b);

  mColorRgb = RGB(r, g, b);
  mBlinkOnTimeMS = 0;
  mBlinkOffTimeMS = 0;

  rgb_to_hsl(mColorRgb.r, mColorRgb.g, mColorRgb.b, mColorHsl.h, mColorHsl.s, mColorHsl.l);

  apply();
}

void LedStrip::setHue(uint8_t h, bool isRelative) {
  float hue = (float)h * mMaxHueInternal / mMaxHueInterface;
  if (isRelative) {
    mColorHsl.h += hue;
  } else {
    mColorHsl.h = hue;
  }

  if (mColorHsl.h > mMaxHueInternal) {
    mColorHsl.h = mColorHsl.h - mMaxHueInternal;
  }
  if (mColorHsl.h < 0.0) {
    mColorHsl.h = mMaxHueInternal - mColorHsl.h;
  }

  apply();
}

void LedStrip::setSaturation(uint8_t s, bool isRelative) {
  float saturation = (float)s / mMaxSaturationInterface;
  if (isRelative) {
    mColorHsl.s += saturation;
  } else {
    mColorHsl.s = saturation;
  }

  if (mColorHsl.s > mMaxSaturationInternal) {
    mColorHsl.s = mColorHsl.s - mMaxSaturationInternal;
  }
  if (mColorHsl.s < 0.0) {
    mColorHsl.s = mMaxSaturationInternal - mColorHsl.s;
  }

  apply();
}

uint32_t LedStrip::getColor() {
  uint8_t rgba[4];
  rgba[0] = mColorRgb.r;
  rgba[1] = mColorRgb.g;
  rgba[2] = mColorRgb.b;
  rgba[3] = 0xFF;
  uint32_t *p32 = (uint32_t *)rgba;
  return *p32;
}

void LedStrip::blink(uint32_t onTimeMS, uint32_t offTimeMS) {
  LOG_INF("LED strip blink for %d - %d", onTimeMS, offTimeMS);

  k_timer_stop(&mLedTimer);

  mBlinkOnTimeMS = onTimeMS;
  mBlinkOffTimeMS = offTimeMS;

  if (mBlinkOnTimeMS != 0 && mBlinkOffTimeMS != 0) {
    scheduleStateChange();
  }
}

void LedStrip::apply() {
  LOG_INF("apply color to original: hsl(%f,%f,%f) state=%d", mColorHsl.h, mColorHsl.s, mColorHsl.l,
          mState);

  int r, g, b;
  hsl_to_rgb(mColorHsl.h, mColorHsl.s, mColorHsl.l, r, g, b);

  struct led_rgb rgb_color = RGB((uint8_t)r, (uint8_t)g, (uint8_t)b);

  LOG_INF("converted color values: rgb(%d,%d,%d) -- int rgb(%d,%d,%d)", rgb_color.r, rgb_color.g,
          rgb_color.b, r, g, b);

  memset(&mPixels, 0x00, sizeof(mPixels));
  if (mState == stateOn) {
    for (auto &pixel : mPixels) {
      memcpy(&pixel, &rgb_color, sizeof(struct led_rgb));
    }
  }

  LOG_HEXDUMP_INF(mPixels, sizeof(struct led_rgb) * pixelCount, "mPixels data");

  int rc = led_strip_update_rgb(strip, mPixels, pixelCount);

  if (rc) {
    LOG_ERR("couldn't update strip: %d", rc);
  }
}

void LedStrip::scheduleStateChange() {
  k_timer_start(&mLedTimer, K_MSEC(mState ? mBlinkOnTimeMS : mBlinkOffTimeMS), K_NO_WAIT);
}

void LedStrip::ledStateTimerHandlerEntry(k_timer *timer) {
  LedStrip *light = reinterpret_cast<LedStrip *>(k_timer_user_data_get(timer));
  light->ledStateTimerHandler();
}

void LedStrip::ledStateTimerHandler() {
  LOG_INF("blink timer timeout. timing: %d,%d", mBlinkOnTimeMS, mBlinkOffTimeMS);

  if (mBlinkOnTimeMS != 0 && mBlinkOffTimeMS != 0) {
    mState = (mState == stateOn) ? stateOff : stateOn;
    apply();
    scheduleStateChange();
  }
}
