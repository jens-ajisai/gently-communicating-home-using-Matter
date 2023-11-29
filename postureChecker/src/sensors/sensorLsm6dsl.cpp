#include "sensorLsm6dsl.h"

#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

#include "sensorAggregator.h"
#include "sensorDataDefinition.h"
#include "sensorInterface.h"

LOG_MODULE_REGISTER(sensorLsm6dsl, CONFIG_SENSORS_LOG_LEVEL);

static inline float out_ev(struct sensor_value *val) {
  return (val->val1 + (float)val->val2 / 1000000);
}

const struct device *SensorLsm6dsl::get_lsm6dsl_device(void) {
  const struct device *const dev = DEVICE_DT_GET_ANY(st_lsm6dsl);

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

int SensorLsm6dsl::init() {
  lsm6dsl = get_lsm6dsl_device();

  if (lsm6dsl == NULL) {
    return -1;
  }

  struct sensor_value odr_attr;
  odr_attr.val1 = 12.5;
  odr_attr.val2 = 0;

  if (sensor_attr_set(lsm6dsl, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_attr) <
      0) {
    LOG_ERR("Cannot set sampling frequency for accelerometer.");
    return -1;
  }

  if (sensor_attr_set(lsm6dsl, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr_attr) <
      0) {
    LOG_ERR("Cannot set sampling frequency for gyro.");
    return -1;
  }
  return 0;
}

int SensorLsm6dsl::getReading(struct posture_data *data) {
  if (lsm6dsl == NULL) {
    data->accel_x = -1;
    data->accel_y = -1;
    data->accel_z = -1;
    data->magn_x = -1;
    data->magn_y = -1;
    data->magn_z = -1;
    return -1;
  }

  struct sensor_value val_x, val_y, val_z;

  sensor_sample_fetch_chan(lsm6dsl, SENSOR_CHAN_ACCEL_XYZ);
  sensor_channel_get(lsm6dsl, SENSOR_CHAN_ACCEL_X, &val_x);
  sensor_channel_get(lsm6dsl, SENSOR_CHAN_ACCEL_Y, &val_y);
  sensor_channel_get(lsm6dsl, SENSOR_CHAN_ACCEL_Z, &val_z);

  LOG_DBG("lsm6dsl accel x: %f y: %f z: %f; ", out_ev(&val_x), out_ev(&val_y),
          out_ev(&val_z));

  data->accel_x = out_ev(&val_x);
  data->accel_y = out_ev(&val_y);
  data->accel_z = out_ev(&val_z);


  sensor_sample_fetch_chan(lsm6dsl, SENSOR_CHAN_MAGN_XYZ);
  sensor_channel_get(lsm6dsl, SENSOR_CHAN_MAGN_X, &val_x);
  sensor_channel_get(lsm6dsl, SENSOR_CHAN_MAGN_Y, &val_y);
  sensor_channel_get(lsm6dsl, SENSOR_CHAN_MAGN_Z, &val_z);

  LOG_DBG("lsm6dsl mag x: %f y: %f z: %f; ", out_ev(&val_x), out_ev(&val_y),
          out_ev(&val_z));

  data->magn_x = out_ev(&val_x);
  data->magn_y = out_ev(&val_y);
  data->magn_z = out_ev(&val_z);
  return 0;
}
