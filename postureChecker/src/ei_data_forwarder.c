/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "ei_data_forwarder.h"

#include <stdio.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ei_data_forwarder, CONFIG_ML_APP_EI_DATA_FORWARDER_LOG_LEVEL);

#if defined(CONFIG_ML_APP_EI_DATA_FORWARDER_UART)
#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usb_device.h>

#define EI_DATA_FORWARDER_UART_NODE DT_CHOSEN(ncs_ml_app_ei_data_forwarder_uart)
#define EI_DATA_FORWARDER_UART_DEV DEVICE_DT_GET(EI_DATA_FORWARDER_UART_NODE)

#define UART_BUF_SIZE CONFIG_ML_APP_EI_DATA_FORWARDER_BUF_SIZE

static const struct device *dev;
#endif

#if defined(CONFIG_ML_APP_EI_DATA_FORWARDER_BT_NUS)
#include <bluetooth/services/nus.h>

#define DATA_BUF_SIZE CONFIG_ML_APP_EI_DATA_FORWARDER_BUF_SIZE
#define DATA_BUF_COUNT CONFIG_ML_APP_EI_DATA_FORWARDER_BUF_COUNT
#define PIPELINE_MAX_CNT CONFIG_ML_APP_EI_DATA_FORWARDER_PIPELINE_COUNT
#define BUF_SLAB_ALIGN 4

struct ei_data_packet {
  sys_snode_t node;
  size_t size;
  uint8_t buf[DATA_BUF_SIZE];
};

static struct bt_conn *nus_conn;

BUILD_ASSERT(sizeof(struct ei_data_packet) % BUF_SLAB_ALIGN == 0);
K_MEM_SLAB_DEFINE(buf_slab, sizeof(struct ei_data_packet), DATA_BUF_COUNT, BUF_SLAB_ALIGN);

static sys_slist_t send_queue;
static struct k_work send_queued;
static size_t pipeline_cnt;
static atomic_t sent_cnt;

static void clean_buffered_data(void) {
  sys_snode_t *node;

  while ((node = sys_slist_get(&send_queue))) {
    struct ei_data_packet *packet = CONTAINER_OF(node, __typeof__(*packet), node);

    k_mem_slab_free(&buf_slab, (void **)&packet);
  }
}

static int send_packet(struct bt_conn *conn, uint8_t *buf, size_t size) {
  int err = 0;
  uint32_t mtu = bt_nus_get_mtu(conn);

  if (mtu < size) {
    LOG_WRN("GATT MTU too small: %zu > %u", size, mtu);
    err = -ENOBUFS;
  } else {
    __ASSERT_NO_MSG(size <= UINT16_MAX);

    err = bt_nus_send(conn, buf, size);

    if (err) {
      LOG_WRN("bt_nus_tx error: %d", err);
    }
  }

  return err;
}

static void send_queued_fn(struct k_work *w) {
  atomic_dec(&sent_cnt);

  if (atomic_get(&sent_cnt)) {
    k_work_submit(&send_queued);
  }

  __ASSERT_NO_MSG(pipeline_cnt > 0);
  pipeline_cnt--;

  __ASSERT_NO_MSG(nus_conn);

  sys_snode_t *node = sys_slist_get(&send_queue);

  if (node) {
    struct ei_data_packet *packet = CONTAINER_OF(node, __typeof__(*packet), node);

    if (send_packet(nus_conn, packet->buf, packet->size)) {
      // nothing
    } else {
      pipeline_cnt++;
    }

    k_mem_slab_free(&buf_slab, (void **)&packet);
  }
}
#endif

int ei_data_forwarder_handle_sensor_event(const struct posture_data data) {
  static uint8_t buf[DATA_BUF_SIZE];

  uint16_t writtenBytes =
      snprintf(buf, DATA_BUF_SIZE, "%u,%d,%d,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\r\n",
               data.time, data.flex1, data.flex2, data.flex3, data.roll, data.pitch, data.yaw,
               data.accel_x, data.accel_y, data.accel_z, data.magn_x, data.magn_y, data.magn_z);
  // https://studio.edgeimpulse.com/studio/276797/acquisition/training
  LOG_DBG("%s",buf);
  

  if (!(writtenBytes >= 0 && writtenBytes < DATA_BUF_SIZE)) {
    LOG_ERR("EI data forwader buffer too small.");
    return -1;
  }

#if defined(CONFIG_ML_APP_EI_DATA_FORWARDER_UART)
  uint32_t cdc_val;
  int err = uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &cdc_val);

  if (err) {
    LOG_WRN("uart_line_ctrl_get(DTR): %d", err);
  }

  if (cdc_val == 0) {
    LOG_DBG("CDC UART disconnected");
  } else {
    int tx_written = uart_fifo_fill(dev, buf, writtenBytes);
    if (tx_written != writtenBytes) {
      LOG_DBG("Sensor data -> CDC overflow");
    }
  }
#endif

#if defined(CONFIG_ML_APP_EI_DATA_FORWARDER_BT_NUS)
  if (nus_conn == NULL) {
    LOG_DBG("nus_conn is null");
  } else {
    if (pipeline_cnt < PIPELINE_MAX_CNT) {
      if (send_packet(nus_conn, buf, writtenBytes)) {
      } else {
        pipeline_cnt++;
      }
    } else {
      struct ei_data_packet *packet;

      if (k_mem_slab_alloc(&buf_slab, (void **)&packet, K_NO_WAIT)) {
        LOG_WRN("No space to buffer data");
        LOG_WRN("Sampling frequency is too high");
      } else {
        memcpy(packet->buf, buf, writtenBytes);
        packet->size = writtenBytes;
        sys_slist_append(&send_queue, &packet->node);
      }
    }
  }
#endif

  return 0;
}

#if defined(CONFIG_ML_APP_EI_DATA_FORWARDER_BT_NUS)
static void bt_nus_sent_cb(struct bt_conn *conn) {
  atomic_inc(&sent_cnt);
  k_work_submit(&send_queued);
}

static void send_enabled(struct bt_conn *conn, enum bt_nus_send_status status) {
  /* Make sure callback and Application Event Manager event handler will not be preempted. */
  BUILD_ASSERT(CONFIG_SYSTEM_WORKQUEUE_PRIORITY < 0);
  __ASSERT_NO_MSG(!k_is_in_isr());
  __ASSERT_NO_MSG(!k_is_preempt_thread());

  if (status == BT_NUS_SEND_STATUS_ENABLED) {
    nus_conn = conn;
  } else {
    nus_conn = NULL;
    clean_buffered_data();
    pipeline_cnt = 0;
    atomic_set(&sent_cnt, 0);
    k_work_cancel(&send_queued);
  }
  LOG_INF("Notifications %sabled", (status == BT_NUS_SEND_STATUS_ENABLED) ? "en" : "dis");
}

static int init_nus(void) {
  static struct bt_nus_cb nus_cb = {
      .sent = bt_nus_sent_cb,
      .send_enabled = send_enabled,
  };

  int err = bt_nus_init(&nus_cb);

  if (err) {
    LOG_ERR("Cannot initialize NUS (err %d)", err);
  }

  return err;
}
#endif

#if defined(CONFIG_ML_APP_EI_DATA_FORWARDER_UART)
static int init_uart(void) {
  dev = EI_DATA_FORWARDER_UART_DEV;

  if (!device_is_ready(dev)) {
    LOG_ERR("UART device binding failed");
    return -ENXIO;
  }

  return 0;
}
#endif

void ei_data_forwarder_init(void) {
#if defined(CONFIG_ML_APP_EI_DATA_FORWARDER_BT_NUS)
  k_work_init(&send_queued, send_queued_fn);
  sys_slist_init(&send_queue);

  init_nus();
#endif

#if defined(CONFIG_ML_APP_EI_DATA_FORWARDER_UART)
  init_uart();
#endif
}
