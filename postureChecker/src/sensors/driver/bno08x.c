/*
  Utilizing code from the Adafruit BNO08x Arduino Library by Bryan Siepert 
  for Adafruit Industries. Found here:
  https://github.com/adafruit/Adafruit_BNO08x

  License:
  Written by Bryan Siepert for Adafruit Industries. 
  MIT license, check license.txt for more information.
  All text above must be included in any redistribution.

  Note: Text above is the complete README.md of the repository.
  I added a copy of the readme to doc/Adafruit_BNO08x/README.md. 
*/

#include <sh2/sh2.h>
#include <sh2/sh2_SensorValue.h>
#include <sh2/sh2_err.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bno08x, CONFIG_MAIN_LOG_LEVEL);

#include "sh2_hal.c"

sh2_Hal_t _HAL = {.open = i2chal_open,
                  .close = i2chal_close,
                  .read = i2chal_read,
                  .write = i2chal_write,
                  .getTimeUs = hal_getTimeUs};

static bool _reset_occurred = false;
static sh2_SensorValue_t *_sensor_value = NULL;

static void hal_callback(void *cookie, sh2_AsyncEvent_t *pEvent) {
  if (pEvent->eventId == SH2_RESET) {
    LOG_DBG("BNO08X Reset");
    _reset_occurred = true;
  }
}

static void sensorHandler(void *cookie, sh2_SensorEvent_t *event) {
  int rc;

  rc = sh2_decodeSensorEvent(_sensor_value, event);
  if (rc != SH2_OK) {
    LOG_ERR("BNO08x - Error decoding sensor event");
    _sensor_value->timestamp = 0;
    return;
  }
}

bool bno08x_init(sh2_ProductIds_t *prodIds) {
  int status;

  LOG_DBG("bno08x_init enter.");

  status = sh2_open(&_HAL, hal_callback, NULL);
  if (status != SH2_OK) {
    return false;
  }

  if (prodIds != NULL) {
    memset(prodIds, 0, sizeof(sh2_ProductIds_t));
    status = sh2_getProdIds(prodIds);
    if (status != SH2_OK) {
      return false;
    }
  }

  sh2_setSensorCallback(sensorHandler, NULL);

  LOG_DBG("bno08x_init exit.");
  return true;
}

bool bno08x_enableReport(sh2_SensorId_t sensorId, uint32_t interval_us) {
  static sh2_SensorConfig_t config;

  // These sensor options are disabled or not used in most cases
  config.changeSensitivityEnabled = false;
  config.wakeupEnabled = false;
  config.changeSensitivityRelative = false;
  config.alwaysOnEnabled = false;
  config.changeSensitivity = 0;
  config.batchInterval_us = 0;
  config.sensorSpecific = 0;

  config.reportInterval_us = interval_us;
  int status = sh2_setSensorConfig(sensorId, &config);

  if (status != SH2_OK) {
    return false;
  }

  return true;
}

bool bno08x_wasReset(void) {
  bool x = _reset_occurred;
  _reset_occurred = false;

  return x;
}

bool bno08x_getSensorEvent(sh2_SensorValue_t *value) {
  _sensor_value = value;

  value->timestamp = 0;

  sh2_service();

  if (value->timestamp == 0 && value->sensorId != SH2_GYRO_INTEGRATED_RV) {
    // no new events
    return false;
  }

  return true;
}