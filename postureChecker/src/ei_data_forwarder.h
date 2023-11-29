#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "sensors/sensorDataDefinition.h"

void ei_data_forwarder_init(void);
int ei_data_forwarder_handle_sensor_event(const struct posture_data data);

#ifdef __cplusplus
}
#endif