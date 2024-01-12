#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/types.h>
#include <zephyr/bluetooth/conn.h>

#include "../../sensors/sensorDataDefinition.h"



#define BT_UUID_EIS_TX_VAL BT_UUID_128_ENCODE(0xe2a00003, 0xec31, 0x4ec3, 0xa97a, 0x1c34d87e9878)
#define BT_UUID_EIS_RX_VAL BT_UUID_128_ENCODE(0xe2a00002, 0xec31, 0x4ec3, 0xa97a, 0x1c34d87e9878)

#define BT_UUID_PCS_VAL BT_UUID_128_ENCODE(0xe5130001, 0x784f, 0x44f3, 0x9e27, 0xab09a4153139)
#define BT_UUID_EIS_VAL BT_UUID_128_ENCODE(0xe2a00001, 0xec31, 0x4ec3, 0xa97a, 0x1c34d87e9878)
#define BT_UUID_PCS_DATA_VAL BT_UUID_128_ENCODE(0xe5130002, 0x784f, 0x44f3, 0x9e27, 0xab09a4153139)
#define BT_UUID_PCS_SCORE_MIN_VAL BT_UUID_128_ENCODE(0xe5130003, 0x784f, 0x44f3, 0x9e27, 0xab09a4153139)
#define BT_UUID_PCS_SCORE_MEA_VAL BT_UUID_128_ENCODE(0xe5130004, 0x784f, 0x44f3, 0x9e27, 0xab09a4153139)
#define BT_UUID_PCS_SCORE_MAX_VAL BT_UUID_128_ENCODE(0xe5130005, 0x784f, 0x44f3, 0x9e27, 0xab09a4153139)
#define BT_UUID_PCS_CONFIG_VAL BT_UUID_128_ENCODE(0xe5130006, 0x784f, 0x44f3, 0x9e27, 0xab09a4153139)

#define BT_UUID_PCS BT_UUID_DECLARE_128(BT_UUID_PCS_VAL)
#define BT_UUID_EIS BT_UUID_DECLARE_128(BT_UUID_EIS_VAL)
#define BT_UUID_PCS_DATA BT_UUID_DECLARE_128(BT_UUID_PCS_DATA_VAL)
#define BT_UUID_PCS_SCORE_MIN BT_UUID_DECLARE_128(BT_UUID_PCS_SCORE_MIN_VAL)
#define BT_UUID_PCS_SCORE_MEA BT_UUID_DECLARE_128(BT_UUID_PCS_SCORE_MEA_VAL)
#define BT_UUID_PCS_SCORE_MAX BT_UUID_DECLARE_128(BT_UUID_PCS_SCORE_MAX_VAL)
#define BT_UUID_PCS_CONFIG BT_UUID_DECLARE_128(BT_UUID_PCS_CONFIG_VAL)

struct pcs_config {
  uint32_t dummy1;
  uint8_t dummy2;
  uint8_t dummy3;
};

typedef void (*config_cb_t)(const struct pcs_config* data);

struct bt_pcs_cb {
  config_cb_t config_cb;
};

int bt_pcs_init(struct bt_pcs_cb *callbacks);

int bt_pcs_send_data(struct bt_conn *conn, struct posture_data data);
int bt_pcs_send_score(struct bt_conn *conn, uint16_t score);

#ifdef __cplusplus
}
#endif
