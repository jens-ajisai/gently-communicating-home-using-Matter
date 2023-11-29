#include "sensorFlex.h"

#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>

#include "sensorAggregator.h"
#include "sensorDataDefinition.h"
#include "sensorInterface.h"

LOG_MODULE_REGISTER(sensorFlex, CONFIG_SENSORS_LOG_LEVEL);

const struct device *SensorFlex::get_ads1015_device() {
  const struct device *const dev = DEVICE_DT_GET_ANY(ti_ads1015);

#pragma GCC diagnostic ignored "-Waddress"
  if (!dev) {
    /* No such node, or the node does not have status "okay". */
    LOG_ERR("Error: no device found.");
    return NULL;
  }
#pragma GCC diagnostic pop

  if (!device_is_ready(dev)) {
    LOG_ERR(
        "Error: Device \"%s\" is not ready; "
        "check the driver initialization logs for errors.",
        dev->name);
    return NULL;
  }

  LOG_INF("Found device \"%s\", getting sensor data\n", dev->name);
  return dev;
}

int SensorFlex::init() {
  ads1015 = get_ads1015_device();

  if (ads1015 == NULL) {
    LOG_ERR("Failed to get ADS1015 device");
    return -1;
  }

  // Since the driver integrated into Zephyr does not support reading multiple channel ids,
  // it is required to reconfigure input_positive to read the correct sensor.
  // There is no call to adc_channel_setup in the init for that reason.
  return 0;
}

int16_t SensorFlex::readPin(uint8_t pin) {
  int16_t buffer = -1;
  if (ads1015 != NULL) {
    const struct adc_channel_cfg ch_cfg = {
        .gain = ADC_GAIN_1,  // ADS1X1X_CONFIG_PGA_2048
        .reference = ADC_REF_INTERNAL,
        .acquisition_time = ADC_ACQ_TIME_DEFAULT,  // ADS1X1X_CONFIG_DR_128_1600
        .channel_id = 0,
        .differential = 0,      // Single ended input, no differential
        .input_positive = pin,  // Activate single ended input for pin
        .input_negative = 0     // Only relevant for differential input
    };

    const struct adc_sequence seq = {.options = NULL,
                                     .channels = BIT(0),
                                     .buffer = &buffer,
                                     .buffer_size = sizeof(buffer),
                                     .resolution = 11,
                                     .oversampling = 0,
                                     .calibrate = 0};

    // always reads ADS1X1X_CONFIG_MODE_SINGLE_SHOT
    adc_channel_setup(ads1015, &ch_cfg);
    adc_read(ads1015, &seq);
  }
  return buffer;
}

int SensorFlex::getReading(struct posture_data *data) {
  data->flex1 = readPin(0);
  data->flex2 = readPin(1);
  data->flex3 = readPin(2);

  LOG_DBG("ADC readings: %d,%d,%d", data->flex1, data->flex2, data->flex3);
  return 0;
}
