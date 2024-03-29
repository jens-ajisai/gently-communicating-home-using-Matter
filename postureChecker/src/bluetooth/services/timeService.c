#include "timeService.h"

#include <bluetooth/bluetooth.h>
#include <bluetooth/conn.h>
#include <bluetooth/gatt.h>
#include <bluetooth/hci.h>
#include <bluetooth/uuid.h>
#include <errno.h>
#include <logging/log.h>
#include <stddef.h>
#include <string.h>
#include <sys/byteorder.h>
#include <sys/printk.h>
#include <zephyr/kernel.h>
#include <zephyr/types.h>

LOG_MODULE_REGISTER(bt_ts, CONFIG_MAIN_LOG_LEVEL);

static struct time_service_cb time_service_cb;

static ssize_t write_set_time(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                              const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
  if (time_service_cb.set_time_cb) {
    if (len == sizeof(uint32_t)) {
      uint8_t *buf8 = (uint8_t *)buf;

      time_t val = 0;
      for (int i = 3; i > 0; i--) {
        val += buf8[i];
        val <<= 8;
      }
      val += buf8[0];
      time_service_cb.set_time_cb(val);
    } else {
      LOG_WRN(
          "Received %d bytes for set time. Need 4 bytes. Give the unix time in little endian."
          " See https://www.epochconverter.com/hex.",
          len);
    }
  }
  return len;
}

int64_t time_default = 0;
BT_GATT_SERVICE_DEFINE(time_service, BT_GATT_PRIMARY_SERVICE(BT_UUID_SERVICE_TIME),
                       BT_GATT_CHARACTERISTIC(BT_UUID_SET_TIME,
                                              BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE |
                                                  BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                                              BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, NULL,
                                              write_set_time, &time_default), );

int time_service_init(struct time_service_cb *callbacks) {
  if (callbacks) {
    time_service_cb.set_time_cb = callbacks->set_time_cb;
  }

  return 0;
}
