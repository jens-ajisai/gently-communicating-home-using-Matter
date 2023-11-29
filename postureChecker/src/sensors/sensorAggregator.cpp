#include "sensorAggregator.h"

#include <zephyr/logging/log.h>

#include "sensorDataDefinition.h"
LOG_MODULE_REGISTER(sensorAggregator, CONFIG_SENSORS_LOG_LEVEL);

SensorAggregator::SensorAggregator(SensorInterface* lsm, SensorInterface* bno,
                                   SensorInterface* flex) {
  LOG_DBG("SensorAggregator enter.");
  sensors.push_back(lsm);
  sensors.push_back(bno);
  sensors.push_back(flex);

  init();
}

int SensorAggregator::init() {
  LOG_DBG("SensorAggregator init enter.");
  for (auto sensor : sensors) {
    sensor->init();
  }
  return 0;
}

int SensorAggregator::collectData(struct posture_data* data) {
  LOG_DBG("SensorAggregator collectData enter.");
  for (auto sensor : sensors) {
    sensor->getReading(data);
  }
  return 0;
}
