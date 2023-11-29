#include "postureCheckerService.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/types.h>

#include "../../sensors/sensorDataDefinition.h"

LOG_MODULE_REGISTER(bt_pcs, CONFIG_MAIN_LOG_LEVEL);

static struct bt_pcs_cb pcs_cb;

static void pcslc_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value) {
  LOG_INF("CCC set to %d for attr handle %d", value, attr->handle);
}

static ssize_t apply_config(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
                            uint16_t len, uint16_t offset, uint8_t flags) {
  LOG_DBG("Attribute write, handle: %u, conn: %p", attr->handle, (void *)conn);
  LOG_DBG("strcmp: %d", strncmp("MF:", buf, 3));

  if (len != sizeof(struct pcs_config)) {
    LOG_ERR("Apply config: Incorrect data length %d of %d", len, sizeof(struct pcs_config));
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
  }

  if (offset != 0) {
    LOG_ERR("Apply config: Incorrect data offset");
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
  }

  if (pcs_cb.config_cb) {
    LOG_HEXDUMP_DBG(buf, len, "Apply config: Would call callback. Data:");
    struct pcs_config *cfg = (struct pcs_config *)buf;
    LOG_DBG("Apply config: convert struct. Data is %d, %d, %d", cfg->dummy1, cfg->dummy2,
            cfg->dummy3);
    pcs_cb.config_cb(cfg);
  }

  return len;
}

static uint16_t min_value = 0;
static uint16_t mea_value = 50;
static uint16_t max_value = 100;

static ssize_t readHandler(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                           uint16_t len, uint16_t offset) {
  char str[BT_UUID_STR_LEN];
  bt_uuid_to_str(attr->uuid, str, sizeof(str));
  uint16_t *value = attr->user_data;
  LOG_DBG("read_input_report uuid %s handle %d len %d offset %d. Return %d", str, attr->handle, len,
          offset, *value);

  return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data, sizeof(uint16_t));
}

/* LED Button Service Declaration */
BT_GATT_SERVICE_DEFINE(
    pcs_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_PCS),
    BT_GATT_CHARACTERISTIC(BT_UUID_PCS_DATA, BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_NONE, NULL, NULL,
                           NULL),
    BT_GATT_CCC(pcslc_ccc_cfg_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
    BT_GATT_CHARACTERISTIC(BT_UUID_PCS_SCORE_MIN, BT_GATT_CHRC_READ, BT_GATT_PERM_READ_AUTHEN,
                           readHandler, NULL, &min_value),
    BT_GATT_CHARACTERISTIC(BT_UUID_PCS_SCORE_MAX, BT_GATT_CHRC_READ, BT_GATT_PERM_READ_AUTHEN,
                           readHandler, NULL, &max_value),
    BT_GATT_CHARACTERISTIC(BT_UUID_PCS_SCORE_MEA, BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_NONE, NULL,
                           NULL, &mea_value),
    BT_GATT_CCC(pcslc_ccc_cfg_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
    BT_GATT_CHARACTERISTIC(BT_UUID_PCS_CONFIG, BT_GATT_CHRC_WRITE, BT_GATT_PERM_WRITE_AUTHEN, NULL,
                           apply_config, NULL), );

int bt_pcs_init(struct bt_pcs_cb *callbacks) {
  if (callbacks) {
    pcs_cb.config_cb = callbacks->config_cb;
  }

  return 0;
}

int bt_pcs_send_data(struct bt_conn *conn, struct posture_data data) {
  struct bt_gatt_notify_params params = {0};
  const struct bt_gatt_attr *attr = &pcs_svc.attrs[2];

  params.attr = attr;
  params.data = &data;
  params.len = sizeof(data);
  params.func = NULL;

  if (!conn) {
    return -ENODEV;
  } else if (bt_gatt_is_subscribed(conn, attr, BT_GATT_CCC_NOTIFY)) {
    return bt_gatt_notify_cb(conn, &params);
  } else {
    return -EINVAL;
  }
}

int bt_pcs_send_score(struct bt_conn *conn, uint16_t score) {
  struct bt_gatt_notify_params params = {0};

  // Avoid magic numbers in sync with BT_GATT_SERVICE_DEFINE here
  // search for the attribute to be changed.
  // We know it should have the user_data of mea_value.
  for (int i = 0; i <= pcs_svc.attr_count; i++) {
    if (pcs_svc.attrs[i].user_data == &mea_value) {
      params.attr = &pcs_svc.attrs[i];
      break;
    }
  }
  params.data = &score;
  params.len = sizeof(score);
  params.func = NULL;

  mea_value = score;

  struct bt_conn_info info;
  bt_conn_get_info(conn, &info);

#if defined(CONFIG_APP_REQUIRE_OOB)
  if ((info.security.flags & BT_SECURITY_FLAG_OOB) == 0) {
    return -BT_ATT_ERR_AUTHENTICATION;
  }
#endif

  if (!conn) {
    return -ENODEV;
  } else if (bt_gatt_is_subscribed(conn, params.attr, BT_GATT_CCC_NOTIFY)) {
    LOG_INF("bt_pcs_send_score %d", score);
    return bt_gatt_notify_cb(conn, &params);
  } else {
    return -EINVAL;
  }
}
