#pragma once

#include "driver/bno08x.h"
#include "sensorDataDefinition.h"
#include "sensorInterface.h"

class SensorBno08x : public SensorInterface {
 public:
  int init();
  int getReading(struct posture_data *);

 private:
  int setReports();

  sh2_SensorValue_t sensorValue;
};
