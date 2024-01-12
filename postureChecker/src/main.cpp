#include <date_time.h>
#include <ei_wrapper.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "bt.h"
#include "ei_data_forwarder.h"
#include "persistence/persistence.h"
#include "sensors/sensorAggregator.h"
#include "sensors/sensorBno08x.h"
#include "sensors/sensorFlex.h"
#include "sensors/sensorLsm6dsl.h"
#include "usb/usb.h"

#if defined(CONFIG_USE_SIMULATION_DATA)
#include "simulation_data.cpp"
#endif

LOG_MODULE_REGISTER(app, CONFIG_MAIN_LOG_LEVEL);

static void app_config_cb(const struct pcs_config *data) {
  LOG_INF("Received config dummy1: %d, dummy2: %d, dummy3: %d", data->dummy1, data->dummy2,
          data->dummy3);
}

static void set_time_cb(time_t val) {
  struct tm *tm_localtime;
  tm_localtime = localtime(&val);

#if defined(CONFIG_DATE_TIME)
  date_time_set(tm_localtime);
  LOG_DBG("Set time to %d %s (%lli)", tm_localtime->tm_year + 1900, asctime(tm_localtime), val);
#else
  LOG_ERR("Cannot set time to %d %s (%lli)", tm_localtime->tm_year + 1900, asctime(tm_localtime),
          val);
#endif
}

static struct bt_app_cb app_callbacks = {
    .config_cb = app_config_cb,
    .time_cb = set_time_cb,
};

int posture_score = 0;
uint8_t posture_step = 5;

static void result_ready_cb(int err) {
  if (err) {
    LOG_ERR("Result ready callback returned error (err: %d)", err);
    return;
  }

  ei_wrapper_start_prediction(0, 1);

  const char *label;
  float value;
  float anomaly;

  err = ei_wrapper_get_next_classification_result(&label, &value, NULL);
  LOG_INF("Value: %.2f\tLabel: %s", value, label);

  if (strcmp(label, "bad") == 0) {
    posture_score += posture_step;
    if (posture_score > 254) posture_score = 254;
  } else if (strcmp(label, "good") == 0) {
    posture_score -= posture_step;
    if (posture_score < 0) posture_score = 0;
  }

  if (ei_wrapper_classifier_has_anomaly()) {
    err = ei_wrapper_get_anomaly(&anomaly);
    if (err) {
      LOG_ERR("Cannot get anomaly (err: %d)", err);
    } else {
      LOG_WRN("Anomaly: %.2f", anomaly);
    }
  }

  LOG_INF("Updated posture score %d", posture_score);
  bt_send_posture_score((uint16_t)posture_score);
}

int main() {
  initUSB();

  Persistence::Instance().init(IS_ENABLED(CONFIG_APP_WIPE_STORAGE));

  SensorLsm6dsl lsm = SensorLsm6dsl();
  SensorBno08x bno = SensorBno08x();
  SensorFlex flex = SensorFlex();
  SensorAggregator agg = SensorAggregator(&bno, &flex, &lsm);

  bt_init(&app_callbacks);

  ei_data_forwarder_init();
  ei_wrapper_init(result_ready_cb);

  //---------
  // Print some information about the model
  LOG_INF("ei frame size: %d", ei_wrapper_get_frame_size());
  LOG_INF("ei window size: %d", ei_wrapper_get_window_size());
  LOG_INF("ei classifier frequency: %d", ei_wrapper_get_classifier_frequency());
  LOG_INF("ei classifier label count: %d", ei_wrapper_get_classifier_label_count());

  for (size_t i = 0; i < ei_wrapper_get_classifier_label_count(); i++) {
    LOG_INF("- %s", ei_wrapper_get_classifier_label(i));
  }
  //---------

  ei_wrapper_start_prediction(0, 0);

  struct posture_data data;
  int64_t t = 0;
#if defined(CONFIG_USE_SIMULATION_DATA)
  unsigned int i = 0;
#endif

  while (true) {
    k_sleep(K_MSEC(500));

    int rc = date_time_now(&t);
    if (rc) {
      data.time = (uint32_t)(k_uptime_get() / 1000);
    } else {
      data.time = (uint32_t)(t / 1000);
    }
    agg.collectData(&data);

    ei_data_forwarder_handle_sensor_event(data);
    Persistence::Instance().writeFile("sensorData.txt", &data, sizeof(data), 0, true);

    bt_send_posture_data(data);

#if defined(CONFIG_USE_SIMULATION_DATA)
    LOG_INF("Send simulation data");
    if (i < ARRAY_SIZE(simulation_data)) {
      ei_wrapper_add_data((float *)&simulation_data[i++],
                          sizeof(struct posture_data_float) / sizeof(float));
    } else {
      i = 0;
    }
#else
    struct posture_data_float float_data = {
        (float)data.flex2, (float)data.flex3, data.roll,   data.pitch,  data.yaw,    data.accel_x,
        data.accel_y,      data.accel_z,      data.magn_x, data.magn_y, data.magn_z,
    };
    ei_wrapper_add_data((float *)&float_data, sizeof(struct posture_data_float) / sizeof(float));
#endif
  }
  return 0;
}
