#pragma once

#include <zephyr/drivers/adc.h>

#include "sensorDataDefinition.h"
#include "sensorInterface.h"

class SensorFlex : public SensorInterface {
 public:
  int init();
  int getReading(struct posture_data *);

 private:
  const struct device *get_ads1015_device();
  int16_t readPin(uint8_t pin);

  const struct device *ads1015;
};
