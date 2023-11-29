#pragma once

#include "sensorDataDefinition.h"

class SensorInterface {
 public:
  virtual int init() = 0;
  virtual int getReading(struct posture_data*) = 0;
};
