#include "sensorBno08x.h"

#include <zephyr/logging/log.h>

#include "driver/bno08x.h"
#include "sensorAggregator.h"
#include "sensorDataDefinition.h"

LOG_MODULE_REGISTER(sensorBno08x, CONFIG_SENSORS_LOG_LEVEL);

// taken from https://en.wikipedia.org/wiki/Conversion_between_quaternions_and_Euler_angles
// then added conversion to angles in degrees
// -- begin
#define _USE_MATH_DEFINES
#include <cmath>

#define M_PI 3.14159265358979323846

struct Quaternion {
  double w, x, y, z;
};

struct EulerAngles {
  double roll, pitch, yaw;
};

EulerAngles ToEulerAngles(Quaternion q) {
  EulerAngles angles;

  // roll (x-axis rotation)
  double sinr_cosp = 2.0f * (q.w * q.x + q.y * q.z);
  double cosr_cosp = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
  angles.roll = std::atan2(sinr_cosp, cosr_cosp);

  // pitch (y-axis rotation)
  double sinp = std::sqrt(1.0f + 2.0f * (q.w * q.y - q.x * q.z));
  double cosp = std::sqrt(1.0f - 2.0f * (q.w * q.y - q.x * q.z));
  angles.pitch = 2 * std::atan2(sinp, cosp) - M_PI / 2;

  // yaw (z-axis rotation)
  double siny_cosp = 2.0f * (q.w * q.z + q.x * q.y);
  double cosy_cosp = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
  angles.yaw = std::atan2(siny_cosp, cosy_cosp);

  // Convert angles to degrees
  angles.yaw = angles.yaw * (180.0 / M_PI);
  angles.pitch = angles.pitch * (180.0 / M_PI);
  angles.roll = angles.roll * (180.0 / M_PI);

  return angles;
}
// -- end


int SensorBno08x::setReports() {
  if (!bno08x_enableReport(SH2_ROTATION_VECTOR, 500 * 1000)) {
    LOG_ERR("Could not enable game vector");
    return -1;
  }
  return 0;
}

int SensorBno08x::init() {
  sh2_ProductIds_t prodIds;

  if (!bno08x_init(&prodIds)) {
    LOG_ERR("Failed to find BNO08x chip");
    return -1;
  }

  LOG_INF("BNO08x Found!");
  for (int n = 0; n < prodIds.numEntries; n++) {
    LOG_INF("Part %d: Version : %d.%d.%d Build %d", prodIds.entry[n].swPartNumber,
            prodIds.entry[n].swVersionMajor, prodIds.entry[n].swVersionMinor,
            prodIds.entry[n].swVersionPatch, prodIds.entry[n].swBuildNumber);
  }
  if (setReports()) {
    return -1;
  }
  return 0;
}

int SensorBno08x::getReading(struct posture_data *data) {
  if (bno08x_wasReset()) {
    LOG_INF("sensor was reset.");
    if (setReports()) {
      return -1;
    }
  }

  if (!bno08x_getSensorEvent(&sensorValue)) {
    data->roll = -1;
    data->pitch = -1;
    data->yaw = -1;
    return -1;
  }

  switch (sensorValue.sensorId) {
    case SH2_ROTATION_VECTOR:
      Quaternion quat = {sensorValue.un.rotationVector.real, sensorValue.un.rotationVector.i,
                         sensorValue.un.rotationVector.j,
                         sensorValue.un.rotationVector.k};  // Replace with your quaternion
      EulerAngles angles = ToEulerAngles(quat);
      LOG_DBG("Absolute Rotation Vector - r: %f i: %f j: %f k: %f",
              sensorValue.un.rotationVector.real, sensorValue.un.rotationVector.i,
              sensorValue.un.rotationVector.j, sensorValue.un.rotationVector.k);
      data->roll = angles.roll;
      data->pitch = angles.pitch;
      data->yaw = angles.yaw;
      break;
  }
  return 0;
}
