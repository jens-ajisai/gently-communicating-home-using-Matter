// based on
// https://github.com/NordicDeveloperAcademy/bt-fund/blob/main/lesson5/blefund_less5_exer2_solution/src/main.c

#include "bt.h"

#include <bluetooth/services/mds.h>
#include <bluetooth/services/nus.h>
#include <stdio.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include "bluetooth/services/postureCheckerService.h"
#include "bluetooth/services/timeService.h"
#include "sensors/sensorDataDefinition.h"
#include "nfc/nfc.h"
LOG_MODULE_REGISTER(bt, CONFIG_MAIN_LOG_LEVEL);

static struct bt_conn *conns[CONFIG_BT_MAX_CONN];

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

struct bt_le_oob oob_local;
struct bt_le_oob oob_remote;

static const char *oob_config_str(int oob_config) {
  switch (oob_config) {
    case BT_CONN_OOB_LOCAL_ONLY:
      return "Local";
    case BT_CONN_OOB_REMOTE_ONLY:
      return "Remote";
    case BT_CONN_OOB_BOTH_PEERS:
      return "Local and Remote";
    case BT_CONN_OOB_NO_DATA:
    default:
      return "none";
  }
}

static void print_le_oob(struct bt_le_oob *oob, char *buffer, uint16_t size) {
  char addr[BT_ADDR_LE_STR_LEN];
  char c[KEY_STR_LEN];
  char r[KEY_STR_LEN];

  bt_addr_le_to_str(&oob->addr, addr, sizeof(addr));

  bin2hex(oob->le_sc_data.c, sizeof(oob->le_sc_data.c), c, sizeof(c));
  bin2hex(oob->le_sc_data.r, sizeof(oob->le_sc_data.r), r, sizeof(r));

  LOG_INF("OOB data:");
  LOG_INF("%-29s %-32s %-32s", "addr", "random", "confirm");
  LOG_INF("%29s %32s %32s", addr, r, c);
  if (buffer && size >= OOB_MESSAGE_LEN) {
    snprintf(buffer, OOB_MESSAGE_LEN, "%29s %32s %32s\n", addr, r, c);
  }
}

int set_remote_oob(const char *addr, const char *type, const char *r, const char *c) {
  bt_addr_le_t bt_addr;

  int err = bt_addr_le_from_str(addr, type, &bt_addr);
  if (err) {
    LOG_ERR("Invalid peer address (err %d)", err);
    return err;
  }

  bt_addr_le_copy(&oob_remote.addr, &bt_addr);
  hex2bin(r, strlen(r), oob_remote.le_sc_data.r, sizeof(oob_remote.le_sc_data.r));
  hex2bin(c, strlen(c), oob_remote.le_sc_data.c, sizeof(oob_remote.le_sc_data.c));
  bt_le_oob_set_sc_flag(true);

  print_le_oob(&oob_remote, NULL, 0);
  return 0;
}

void local_oob_get(char *buffer, uint16_t size) {
  int err = bt_le_oob_get_local(BT_ID_DEFAULT, &oob_local);
  if (err) {
    LOG_ERR("Retrieving OOB data failed (err %d)", err);
    return;
  }

  print_le_oob(&oob_local, buffer, size);
}

static void auth_pairing_oob_data_request(struct bt_conn *conn, struct bt_conn_oob_info *oob_info) {
  char addr[BT_ADDR_LE_STR_LEN];
  struct bt_conn_info info;
  int err;

  err = bt_conn_get_info(conn, &info);
  if (err) {
    return;
  }

  if (oob_info->type == BT_CONN_OOB_LE_SC) {
    struct bt_le_oob_sc_data *oobd_local =
        oob_info->lesc.oob_config != BT_CONN_OOB_REMOTE_ONLY ? &oob_local.le_sc_data : NULL;
    struct bt_le_oob_sc_data *oobd_remote =
        oob_info->lesc.oob_config != BT_CONN_OOB_LOCAL_ONLY ? &oob_remote.le_sc_data : NULL;

    if (oobd_remote && !bt_addr_le_eq(info.le.remote, &oob_remote.addr)) {
      bt_addr_le_to_str(info.le.remote, addr, sizeof(addr));
      LOG_INF("No OOB data available for remote %s", addr);
      bt_conn_auth_cancel(conn);
      return;
    }

    if (oobd_local && !bt_addr_le_eq(info.le.local, &oob_local.addr)) {
      bt_addr_le_to_str(info.le.local, addr, sizeof(addr));
      LOG_INF("No OOB data available for local %s", addr);
      bt_conn_auth_cancel(conn);
      return;
    }

    bt_le_oob_set_sc_data(conn, oobd_local, oobd_remote);

    bt_addr_le_to_str(info.le.dst, addr, sizeof(addr));
    LOG_INF("Set %s OOB SC data for %s, ", oob_config_str(oob_info->lesc.oob_config), addr);
    return;
  }

  bt_addr_le_to_str(info.le.dst, addr, sizeof(addr));
  LOG_ERR("Legacy OOB not supported! TK requested from remote %s", addr);
}

#define BT_LE_ADV_CONN_NO_ACCEPT_LIST                                                            \
  BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_USE_IDENTITY | BT_LE_ADV_OPT_ONE_TIME, BT_GAP_ADV_FAST_INT_MIN_2, \
                  BT_GAP_ADV_FAST_INT_MAX_2, NULL)

#define BT_LE_ADV_CONN_ACCEPT_LIST                                                                \
  BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_USE_IDENTITY | BT_LE_ADV_OPT_FILTER_CONN | BT_LE_ADV_OPT_ONE_TIME, \
                  BT_GAP_ADV_FAST_INT_MIN_2, BT_GAP_ADV_FAST_INT_MAX_2, NULL)



// advertisment data
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_MDS_VAL),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

// scan response data
// Advertisment of two 128 bit UUIDs is too large for standard advertisments.
// Therefore only use the posture chekcer service value
static const struct bt_data sd[] = {
//    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NUS_VAL),
//    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_EIS_VAL),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_PCS_VAL),
};

static void setup_accept_list_cb(const struct bt_bond_info *info, void *user_data) {
  int *bond_cnt = user_data;

  if ((*bond_cnt) < 0) {
    return;
  }

  int err = bt_le_filter_accept_list_add(&info->addr);
  LOG_INF("Added following peer to accept list: %x %x", info->addr.a.val[0], info->addr.a.val[1]);
  if (err) {
    LOG_INF("Cannot add peer to filter accept list (err: %d)", err);
    (*bond_cnt) = -EIO;
  } else {
    (*bond_cnt)++;
  }
}

static int setup_accept_list(uint8_t local_id) {
  int err = bt_le_filter_accept_list_clear();

  if (err) {
    LOG_INF("Cannot clear accept list (err: %d)", err);
    return err;
  }

  int bond_cnt = 0;

  bt_foreach_bond(local_id, setup_accept_list_cb, &bond_cnt);

  return bond_cnt;
}

void advertise_with_acceptlist(struct k_work *work) {
  int err = 0;

  err = bt_le_adv_stop();
  if (err) {
    LOG_INF("Cannot stop advertising err= %d \n", err);
    return;
  }

  int allowed_cnt = setup_accept_list(BT_ID_DEFAULT);
  if (allowed_cnt < 0) {
    LOG_INF("Acceptlist setup failed (err:%d)", allowed_cnt);
  } else {
    if (allowed_cnt == 0) {
      LOG_INF("Advertising with no Accept list ");
      err = bt_le_adv_start(BT_LE_ADV_CONN_NO_ACCEPT_LIST, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    } else {
      LOG_INF("Acceptlist setup number  = %d ", allowed_cnt);
#if defined(CONFIG_APP_USER_FILTER_ACCESS_LIST)
      err = bt_le_adv_start(BT_LE_ADV_CONN_ACCEPT_LIST, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
#else
      LOG_WRN("Ignore acceptlist.");
      err = bt_le_adv_start(BT_LE_ADV_CONN_NO_ACCEPT_LIST, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
#endif
    }
    if (err) {
      LOG_INF("Advertising failed to start (err %d)", err);
      return;
    }
    LOG_INF("Advertising successfully started");

    local_oob_get(0, 0);
    nfc_init();    
  }
}

K_WORK_DEFINE(advertise_acceptlist_work, advertise_with_acceptlist);

/*
static void update_phy(struct bt_conn *conn) {
  int err;
  const struct bt_conn_le_phy_param preferred_phy = {
      .options = BT_CONN_LE_PHY_OPT_NONE,
      .pref_rx_phy = BT_GAP_LE_PHY_2M,
      .pref_tx_phy = BT_GAP_LE_PHY_2M,
  };
  err = bt_conn_le_phy_update(conn, &preferred_phy);
  if (err) {
    LOG_ERR("bt_conn_le_phy_update() returned %d", err);
  }
}
*/

static void update_data_length(struct bt_conn *conn) {
  int err;
  struct bt_conn_le_data_len_param my_data_len = {
      .tx_max_len = BT_GAP_DATA_LEN_MAX,
      .tx_max_time = BT_GAP_DATA_TIME_MAX,
  };
  err = bt_conn_le_data_len_update(conn, &my_data_len);
  if (err) {
    LOG_ERR("data_len_update failed (err %d)", err);
  }
}

static void exchange_func(struct bt_conn *conn, uint8_t att_err,
                          struct bt_gatt_exchange_params *params) {
  LOG_INF("MTU exchange %s", att_err == 0 ? "successful" : "failed");
  if (!att_err) {
    uint16_t payload_mtu = bt_gatt_get_mtu(conn) - 3;  // 3 bytes used for Attribute headers.
    LOG_INF("New MTU: %d bytes", payload_mtu);
  }
}

static void update_mtu(struct bt_conn *conn) {
  int err;

  static struct bt_gatt_exchange_params exchange_params;
  exchange_params.func = exchange_func;

  err = bt_gatt_exchange_mtu(conn, &exchange_params);
  if (err) {
    LOG_ERR("bt_gatt_exchange_mtu failed (err %d)", err);
  }
}

static void on_connected(struct bt_conn *conn, uint8_t err) {
  char addr[BT_ADDR_LE_STR_LEN];

  if (err) {
    LOG_INF("Connection failed (err %u)", err);
    return;
  }

  for (size_t i = 0; i < ARRAY_SIZE(conns); i++) {
    if (!conns[i]) {
      conns[i] = bt_conn_ref(conn);
      break;
    }
  }

  struct bt_conn_info info;
  err = bt_conn_get_info(conn, &info);
  if (err) {
    LOG_ERR("bt_conn_get_info() returned %d", err);
    return;
  }

  double connection_interval = info.le.interval * 1.25;  // in ms
  uint16_t supervision_timeout = info.le.timeout * 10;   // in ms
  LOG_INF("Connection parameters: interval %.2f ms, latency %d intervals, timeout %d ms",
          connection_interval, info.le.latency, supervision_timeout);

  // Fails to LE Set PHY (-5)
//  update_phy(conn);
  update_data_length(conn);
  update_mtu(conn);

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  LOG_INF("Connected to %s", addr);
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason) {
  LOG_INF("Disconnected (reason %u)", reason);

  for (size_t i = 0; i < ARRAY_SIZE(conns); i++) {
    if (conns[i] == conn) {
      bt_conn_unref(conns[i]);
      conns[i] = NULL;
      break;
    }
  }

  k_work_submit(&advertise_acceptlist_work);
}

static void on_security_changed(struct bt_conn *conn, bt_security_t level,
                                enum bt_security_err err) {
  char addr[BT_ADDR_LE_STR_LEN];
  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  struct bt_conn_info info;
  bt_conn_get_info(conn, &info);

  if (!err) {
    LOG_INF("Security changed: %s level %u", addr, level);
    LOG_INF("  ... Security level: %d, flag: %d", info.security.level, info.security.flags);
  } else {
    LOG_INF("Security failed: %s level %u err %d", addr, level, err);
  }
}

void on_le_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency,
                         uint16_t timeout) {
  double connection_interval = interval * 1.25;  // in ms
  uint16_t supervision_timeout = timeout * 10;   // in ms
  LOG_INF("Connection parameters updated: interval %.2f ms, latency %d intervals, timeout %d ms",
          connection_interval, latency, supervision_timeout);
}

void on_le_phy_updated(struct bt_conn *conn, struct bt_conn_le_phy_info *param) {
  if (param->tx_phy == BT_CONN_LE_TX_POWER_PHY_1M) {
    LOG_INF("PHY updated. New PHY: 1M");
  } else if (param->tx_phy == BT_CONN_LE_TX_POWER_PHY_2M) {
    LOG_INF("PHY updated. New PHY: 2M");
  } else if (param->tx_phy == BT_CONN_LE_TX_POWER_PHY_CODED_S8) {
    LOG_INF("PHY updated. New PHY: Long Range");
  }
}

void on_le_data_len_updated(struct bt_conn *conn, struct bt_conn_le_data_len_info *info) {
  uint16_t tx_len = info->tx_max_len;
  uint16_t tx_time = info->tx_max_time;
  uint16_t rx_len = info->rx_max_len;
  uint16_t rx_time = info->rx_max_time;
  LOG_INF("Data length updated. Length %d/%d bytes, time %d/%d us", tx_len, rx_len, tx_time,
          rx_time);
}

static bool on_le_param_req(struct bt_conn *conn, struct bt_le_conn_param *param) {
  LOG_INF("Connection parameters update request received.");
  LOG_INF("Minimum interval: %d, Maximum interval: %d", param->interval_min, param->interval_max);
  LOG_INF("Latency: %d, Timeout: %d", param->latency, param->timeout);

  return true;
}

static void on_identity_resolved(struct bt_conn *conn, const bt_addr_le_t *rpa,
                                 const bt_addr_le_t *identity) {
  char addr_identity[BT_ADDR_LE_STR_LEN];
  char addr_rpa[BT_ADDR_LE_STR_LEN];

  bt_addr_le_to_str(identity, addr_identity, sizeof(addr_identity));
  bt_addr_le_to_str(rpa, addr_rpa, sizeof(addr_rpa));

  LOG_INF("BT Identity resolved %s -> %s.", addr_rpa, addr_identity);
}

struct bt_conn_cb connection_callbacks = {
    .connected = on_connected,
    .disconnected = on_disconnected,
    .security_changed = on_security_changed,
    .le_param_updated = on_le_param_updated,
    .le_phy_updated = on_le_phy_updated,
    .le_data_len_updated = on_le_data_len_updated,
    .le_param_req = on_le_param_req,
    .identity_resolved = on_identity_resolved,
};

static void pairing_complete(struct bt_conn *conn, bool bonded) {
  char addr[BT_ADDR_LE_STR_LEN];

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  printk("Pairing completed: %s, bonded: %d\n", addr, bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason) {
  char addr[BT_ADDR_LE_STR_LEN];

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  printk("Pairing failed conn: %s, reason %d\n", addr, reason);
}

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {.pairing_complete = pairing_complete,
                                                               .pairing_failed = pairing_failed};

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey) {
  char addr[BT_ADDR_LE_STR_LEN];

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  LOG_INF("Passkey for %s: %06u", addr, passkey);
}

static void auth_passkey_confirm(struct bt_conn *conn, unsigned int passkey) {
  char addr[BT_ADDR_LE_STR_LEN];
  char passkey_str[7];

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  snprintk(passkey_str, 7, "%06u", passkey);

  LOG_INF("Confirm passkey for %s: %s", addr, passkey_str);

  // automatic confirmation. Bad practice, better not define this function.
  // by defining this callback, we tell the bt stack that
  //   the device supports a display to show the number
  //   and an input device to confirm the number.
  //   But it doesn't have any capabilities.
  bt_conn_auth_passkey_confirm(conn);
}

static void auth_cancel(struct bt_conn *conn) {
  char addr[BT_ADDR_LE_STR_LEN];

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  LOG_INF("Pairing cancelled: %s", addr);
}

static void auth_pairing_confirm(struct bt_conn *conn) {
  char addr[BT_ADDR_LE_STR_LEN];

  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

  LOG_INF("Confirm pairing for %s", addr);

  bt_conn_auth_pairing_confirm(conn);
}

#if defined(CONFIG_BT_SMP_APP_PAIRING_ACCEPT)
static enum bt_security_err pairing_accept(struct bt_conn *conn,
                                           const struct bt_conn_pairing_feat *const feat) {
  LOG_INF(
      "Remote pairing features: "
      "IO: 0x%02x, OOB: %d, AUTH: 0x%02x, Key: %d, "
      "Init Kdist: 0x%02x, Resp Kdist: 0x%02x",
      feat->io_capability, feat->oob_data_flag, feat->auth_req, feat->max_enc_key_size,
      feat->init_key_dist, feat->resp_key_dist);

#if defined(CONFIG_APP_REQUIRE_OOB)
    if(feat->oob_data_flag == 0) {
      return BT_SECURITY_ERR_OOB_NOT_AVAILABLE;
    }
#endif

  return BT_SECURITY_ERR_SUCCESS;
}
#endif /* CONFIG_BT_SMP_APP_PAIRING_ACCEPT */

static struct bt_conn_auth_cb conn_auth_callbacks = {
    .passkey_display = auth_passkey_display,
    .passkey_confirm = auth_passkey_confirm,
    .oob_data_request = auth_pairing_oob_data_request,
    .cancel = auth_cancel,
    .pairing_confirm = auth_pairing_confirm,
#if defined(CONFIG_BT_SMP_APP_PAIRING_ACCEPT)
    .pairing_accept = pairing_accept,
#endif
};

#if defined(CONFIG_BT_MDS)
static bool mds_access_enable(struct bt_conn *conn) {
  struct bt_conn_info info;
  bt_conn_get_info(conn, &info);

#if defined(CONFIG_APP_REQUIRE_OOB)
  if ((info.security.flags & BT_SECURITY_FLAG_OOB) == 0) {
    return false;
  }
#endif

  return true;
}

static const struct bt_mds_cb mds_cb = {
    .access_enable = mds_access_enable,
};
#endif

void advertise_without_filter() {
  int err_code = bt_le_adv_stop();
  if (err_code) {
    LOG_INF("Cannot stop advertising err= %d \n", err_code);
    return;
  }
  err_code = bt_le_filter_accept_list_clear();
  if (err_code) {
    LOG_INF("Cannot clear accept list (err: %d)\n", err_code);
  } else {
    LOG_INF("Accept list cleared succesfully");
  }
  err_code = bt_le_adv_start(BT_LE_ADV_CONN_NO_ACCEPT_LIST, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

  if (err_code) {
    LOG_INF("Cannot start open advertising (err: %d)\n", err_code);
  } else {
    LOG_INF("Advertising in pairing mode started");
  }
}

void remove_bonding() { bt_unpair(BT_ID_DEFAULT, BT_ADDR_LE_ANY); }

// for rand()
#include <stdlib.h>

void bt_send_posture_data(struct posture_data data) {
  for (size_t i = 0; i < ARRAY_SIZE(conns); i++) {
    if (conns[i]) {
      // Try all connections.
      // The check if subscribed and if security level is sufficient
      // is inside these functions.
      bt_pcs_send_data(conns[i], data);
    }
  }
}

void bt_send_posture_score(uint16_t score) {
  for (size_t i = 0; i < ARRAY_SIZE(conns); i++) {
    if (conns[i]) {
      // Try all connections.
      // The check if subscribed and if security level is sufficient
      // is inside these functions.
      bt_pcs_send_score(conns[i], score);
    }
  }
}

void bt_init(struct bt_app_cb *cb) {
  int err;

#if defined(CONFIG_BT_MDS)
  err = bt_mds_cb_register(&mds_cb);
  if (err) {
    printk("Memfault Diagnostic service callback registration failed (err %d)\n", err);
    return;
  }
#endif

  err = bt_conn_auth_cb_register(&conn_auth_callbacks);
  if (err) {
    LOG_ERR("Failed to register authorization callbacks.");
    return;
  }

  err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
  if (err) {
    LOG_ERR("Failed to register authorization info callbacks (err %d)", err);
    return;
  }

  bt_conn_cb_register(&connection_callbacks);

  err = bt_enable(NULL);
  if (err) {
    LOG_ERR("Bluetooth init failed (err %d)", err);
    return;
  }

  static struct bt_pcs_cb bt_callbacks;
  if (cb) {
    bt_callbacks.config_cb = cb->config_cb;
  }

  err = bt_pcs_init(&bt_callbacks);
  if (err) {
    LOG_ERR("Failed to init PCS (err:%d)", err);
    return;
  }

  static struct time_service_cb time_service_callbacs;
  if (cb) {
    time_service_callbacs.set_time_cb = cb->time_cb;
  }

  err = time_service_init(&time_service_callbacs);
  if (err) {
    LOG_ERR("Failed to initialize time service (err: %d)", err);
    return;
  }

  LOG_INF("Bluetooth initialized");

  if (IS_ENABLED(CONFIG_SETTINGS)) {
    err = settings_load();
    if (err) {
      LOG_ERR("Failed to load settings (err %d)", err);
      return;
    }
  }

  k_work_submit(&advertise_acceptlist_work);
}
