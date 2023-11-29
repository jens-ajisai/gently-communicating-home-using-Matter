#pragma once

#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>

#define RGB(_r, _g, _b) \
  { .r = (_r), .g = (_g), .b = (_b) }

#define HSL(_h, _s, _l) \
  { .h = (_h), .s = (_s), .l = (_l) }

#define COLOR_RGB_WHITE RGB(0xff, 0xff, 0xff)
#define COLOR_HSL_GREEN HSL(85.0, 1.0, 0.5)
#define COLOR_HSL_WHITE HSL(85.0, 1.0, 1.0)
#define LED_DEVICE_NODE DT_ALIAS(led_strip)

struct led_hsl {
  float h; // range 0.0 - 360.0
  float s; // range 0.0 -   1.0
  float l; // range 0.0 -   1.0
};

class LedStrip {
 public:
  void init(uint8_t minLevel, uint8_t maxLevel);
  void enable(bool on);
  void setLevel(uint8_t level);
  void setColor(uint32_t rgba);
  void setColor(uint8_t r, uint8_t g, uint8_t b);
  void setHue(uint8_t h, bool isRelative);
  void setSaturation(uint8_t s, bool isRelative);
  void blink(uint32_t onTimeMS, uint32_t offTimeMS);
  bool isTurnedOn() const { return mState == stateOn; }
  uint8_t getLevel() const { return (uint8_t)(mColorHsl.l * mMaxLevel); }
  uint8_t getMinLevel() const { return mMinLevel; }
  uint8_t getMaxLevel() const { return mMaxLevel; }
  uint32_t getColor();
  uint8_t getHue() {return (uint8_t)(mColorHsl.h / mMaxHueInternal * mMaxHueInterface); }
  uint8_t getSaturation() {return (uint8_t)(mColorHsl.s * mMaxSaturationInterface); }
  static void ledStateTimerHandlerEntry(k_timer *timer);
  void ledStateTimerHandler();

 private:
  void apply();
  void scheduleStateChange();

  static constexpr uint16_t pixelCount = DT_PROP(DT_ALIAS(led_strip), chain_length);

  enum State_t : uint8_t {
    stateOn = 0,
    stateOff,
  };

  k_timer mLedTimer;
  k_timer mUpdateTimer;
  uint32_t mBlinkOnTimeMS;
  uint32_t mBlinkOffTimeMS;

  State_t mState;
  uint8_t mMinLevel;
  uint8_t mMaxLevel;
  uint8_t mMaxHueInterface = 254;
  float mMaxHueInternal = 360.0;
  uint8_t mMaxSaturationInterface = 254;
  float mMaxSaturationInternal = 1.0;
  struct led_rgb mColorRgb;
  struct led_hsl mColorHsl;

  const struct device *const strip = DEVICE_DT_GET(LED_DEVICE_NODE);
  struct led_rgb mPixels[pixelCount];
};