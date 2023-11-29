#pragma once

#include <zephyr/kernel.h>

#include <vector>

#include "sensorDataDefinition.h"
#include "sensorInterface.h"

class SensorAggregator {
 public:
  SensorAggregator(SensorInterface* lsm, SensorInterface* bno, SensorInterface* flex);

  int collectData(struct posture_data* data);

 private:
  int init();
  std::vector<SensorInterface*> sensors;
};
