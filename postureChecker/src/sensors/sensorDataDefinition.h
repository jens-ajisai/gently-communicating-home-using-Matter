#pragma once

#include <zephyr/kernel.h>

struct posture_data {
  uint32_t time;
  uint16_t flex1;
  uint16_t flex2;
  uint16_t flex3;
  float roll;
  float pitch;
  float yaw;
  float accel_x;
  float accel_y;
  float accel_z;
  float magn_x;
  float magn_y;
  float magn_z;
};

struct posture_data_float {
  float flex2;
  float flex3;
  float roll;
  float pitch;
  float yaw;
  float accel_x;
  float accel_y;
  float accel_z;
  float magn_x;
  float magn_y;
  float magn_z;
};
