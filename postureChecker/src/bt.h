#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/bluetooth/addr.h>

#include "bluetooth/services/postureCheckerService.h"
#include "bluetooth/services/timeService.h"
#include "sensors/sensorDataDefinition.h"

struct bt_app_cb {
  config_cb_t config_cb;
  set_time_cb_t time_cb;
};

#define KEY_STR_LEN (33)
#define OOB_MESSAGE_LEN (BT_ADDR_LE_STR_LEN + KEY_STR_LEN + KEY_STR_LEN + 1)

void bt_init(struct bt_app_cb *cb);

void bt_send_posture_data(struct posture_data data);
void bt_send_posture_score(uint16_t score);
void advertise_without_filter();

void remove_bonding();
int set_remote_oob(const char *addr, const char *type, const char *r, const char *c);
void local_oob_get(char *buffer, uint16_t size);

#ifdef __cplusplus
}
#endif
