/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>

#include <date_time.h>

#include "../../ai/whisper.h"
#include "../../ai/completions.h"

LOG_MODULE_REGISTER(network, CONFIG_MAIN_LOG_LEVEL);
#include "net_private.h"

static struct net_mgmt_event_callback net_mgmt_callback;
static struct net_mgmt_event_callback net_mgmt_ipv4_callback;
static struct net_mgmt_event_callback net_mgmt_dhcp_callback;

static void onConnected(struct k_work *work) {
	date_time_update_async(NULL);
}

static struct k_work_delayable onConnectedWork;

static int get_wifi_status(void) {
  struct net_if *iface = net_if_get_default();
  struct wifi_iface_status status = {0};

  if (net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status, sizeof(struct wifi_iface_status))) {
    LOG_INF("Status request failed");

    return -ENOEXEC;
  }

  LOG_INF("==================");
  LOG_INF("State: %s", wifi_state_txt(status.state));

  if (status.state >= WIFI_STATE_ASSOCIATED) {
    uint8_t mac_string_buf[sizeof("xx:xx:xx:xx:xx:xx")];

    LOG_INF("Interface Mode: %s", wifi_mode_txt(status.iface_mode));
    LOG_INF("Link Mode: %s", wifi_link_mode_txt(status.link_mode));
    LOG_INF("SSID: %-32s", status.ssid);
    LOG_INF("BSSID: %s", net_sprint_ll_addr_buf(status.bssid, WIFI_MAC_ADDR_LEN, mac_string_buf,
                                                sizeof(mac_string_buf)));
    LOG_INF("Band: %s", wifi_band_txt(status.band));
    LOG_INF("Channel: %d", status.channel);
    LOG_INF("Security: %s", wifi_security_txt(status.security));
    LOG_INF("MFP: %s", wifi_mfp_txt(status.mfp));
    LOG_INF("RSSI: %d", status.rssi);
  }
  return 0;
}

static int __wifi_args_to_params(struct wifi_connect_req_params *params) {
  params->timeout = SYS_FOREVER_MS;

  /* SSID */
  params->ssid = CONFIG_STA_SAMPLE_SSID;
  params->ssid_length = strlen(params->ssid);

#if defined(CONFIG_STA_KEY_MGMT_WPA2)
  params->security = 1;
#elif defined(CONFIG_STA_KEY_MGMT_WPA2_256)
  params->security = 2;
#elif defined(CONFIG_STA_KEY_MGMT_WPA3)
  params->security = 3;
#else
  params->security = 0;
#endif

#if !defined(CONFIG_STA_KEY_MGMT_NONE)
  params->psk = CONFIG_STA_SAMPLE_PASSWORD;
  params->psk_length = strlen(params->psk);
#endif
  params->channel = WIFI_CHANNEL_ANY;

  /* MFP (optional) */
  params->mfp = WIFI_MFP_OPTIONAL;

  return 0;
}

static void connect(void) {
  struct net_if *iface = net_if_get_default();

  static struct wifi_connect_req_params cnx_params;
  __wifi_args_to_params(&cnx_params);

  if (iface == NULL) {
    LOG_ERR("Returned network interface is NULL");
    return;
  }

  int err = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &cnx_params,
                     sizeof(struct wifi_connect_req_params));

  if (err) {
    LOG_ERR("Connecting to Wi-Fi failed. error: %d", err);
  }
}

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event,
                                    struct net_if *iface) {
  const struct wifi_status *wifi_status = (const struct wifi_status *)cb->info;

  switch (mgmt_event) {
    case NET_EVENT_WIFI_CONNECT_RESULT:
      if (wifi_status->status) {
        LOG_INF("Connection attempt failed, status code: %d", wifi_status->status);
        return;
      }
      LOG_INF("Wi-Fi Connected, waiting for IP address");
      break;
    case NET_EVENT_WIFI_DISCONNECT_RESULT:
      LOG_INF("Disconnected");
      break;
    default:
      LOG_ERR("Unknown event: %d", mgmt_event);
      return;
  }

  get_wifi_status();
}

static void ipv4_mgmt_event_handler(struct net_mgmt_event_callback *cb, uint32_t event,
                                    struct net_if *iface) {
  switch (event) {
    case NET_EVENT_IPV4_ADDR_ADD:
      LOG_INF("IPv4 address acquired");
      k_work_init_delayable(&onConnectedWork, onConnected);
      k_work_reschedule(&onConnectedWork, K_NO_WAIT);
      break;
    case NET_EVENT_IPV4_ADDR_DEL:
      LOG_INF("IPv4 address lost");
      break;
    case NET_EVENT_IPV4_DHCP_START:
      LOG_INF("IPv4 DHCP start");
      break;
    case NET_EVENT_IPV4_DHCP_STOP:
      LOG_INF("IPv4 DHCP stop");
      break;
    default:
      LOG_DBG("Unknown event: 0x%08X", event);
      return;
  }
}

static void print_dhcp_ip(struct net_mgmt_event_callback *cb) {
  /* Get DHCP info from struct net_if_dhcpv4 and print */
  const struct net_if_dhcpv4 *dhcpv4 = cb->info;
  const struct in_addr *addr = &dhcpv4->requested_ip;
  char dhcp_info[128];

  net_addr_ntop(AF_INET, addr, dhcp_info, sizeof(dhcp_info));

  LOG_INF("DHCP IP address: %s", dhcp_info);
}

static void net_mgmt_event_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event,
                                   struct net_if *iface) {
  switch (mgmt_event) {
    case NET_EVENT_IPV4_DHCP_BOUND:
      print_dhcp_ip(cb);
      break;
    default:
      break;
  }
}

static void network_task(void) {
  net_mgmt_init_event_callback(&net_mgmt_callback, wifi_mgmt_event_handler,
                               NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT);
  net_mgmt_add_event_callback(&net_mgmt_callback);

  net_mgmt_init_event_callback(&net_mgmt_ipv4_callback, ipv4_mgmt_event_handler,
                               NET_EVENT_IPV4_ADDR_ADD | NET_EVENT_IPV4_ADDR_DEL);
  net_mgmt_add_event_callback(&net_mgmt_ipv4_callback);

  net_mgmt_init_event_callback(&net_mgmt_dhcp_callback, net_mgmt_event_handler,
                               NET_EVENT_IPV4_DHCP_BOUND);
  net_mgmt_add_event_callback(&net_mgmt_dhcp_callback);

  LOG_INF("Starting %s with CPU frequency: %d MHz", CONFIG_BOARD, SystemCoreClock / MHZ(1));
  LOG_INF("Static IP address (overridable): %s/%s -> %s", CONFIG_NET_CONFIG_MY_IPV4_ADDR,
          CONFIG_NET_CONFIG_MY_IPV4_NETMASK, CONFIG_NET_CONFIG_MY_IPV4_GW);

  /* Add temporary fix to prevent using Wi-Fi before WPA supplicant is ready. */
  // Increased to 3 seconds as 1 second can fail.
  k_sleep(K_SECONDS(1));

  connect();
}

K_THREAD_DEFINE(network_task_id, 4096, network_task, NULL, NULL, NULL, 3, 0, 0);
