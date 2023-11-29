#pragma once

#include <zephyr/drivers/adc.h>

#include "sensorDataDefinition.h"
#include "sensorInterface.h"

class SensorLsm6dsl : public SensorInterface {
 public:
  int init();
  int getReading(struct posture_data *);

 private:
  const struct device *get_lsm6dsl_device();

  const struct device *lsm6dsl;
};
