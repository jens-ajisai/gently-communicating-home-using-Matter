#include "ca_certificate.h"
#include "socket_common.h"

#include <zephyr/logging/log.h>
#include <zephyr/net/dns_resolve.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
LOG_MODULE_REGISTER(socket_common, CONFIG_CHIP_APP_LOG_LEVEL);

// Not really part of socket, but common for the http client users
uint8_t recv_buf_ipv4[MAX_RECV_BUF_LEN];
uint8_t send_buf_ipv4[MAX_SEND_BUF_LEN];

// Magic global variable that holds the results of the requests to open.ai
char response_buf[MAX_EXPECTED_RESPONSE_BODY];

#define DNS_TIMEOUT (5 * MSEC_PER_SEC)

static struct k_poll_signal dnsFinishedSignal;
static struct k_poll_event dnsFinishedEvents[1];

static void dns_result_cb(enum dns_resolve_status status, struct dns_addrinfo *info,
                          void *user_data) {
  struct sockaddr *addr = (struct sockaddr *)user_data;

  switch (status) {
    case DNS_EAI_CANCELED:
      LOG_ERR("DNS query was canceled");
      return;
    case DNS_EAI_FAIL:
      LOG_ERR("DNS resolve failed");
      return;
    case DNS_EAI_NODATA:
      LOG_ERR("Cannot resolve address");
      return;
    case DNS_EAI_ALLDONE:
      LOG_ERR("DNS resolving finished");
      return;
    case DNS_EAI_INPROGRESS:
      break;
    default:
      LOG_ERR("DNS resolving error (%d)", status);
      return;
  }

  if (!info) {
    return;
  }

  if (info->ai_family == AF_INET) {
    net_sin(addr)->sin_family = info->ai_family;
    memcpy(&net_sin(addr)->sin_addr, &net_sin(&info->ai_addr)->sin_addr, sizeof(struct in_addr));
  } else {
    LOG_ERR("Invalid IP address family %d", info->ai_family);
    return;
  }

  char hr_addr[NET_IPV4_ADDR_LEN];
  LOG_INF(
      "dns address resolved to: %s",
      net_addr_ntop(info->ai_family, &net_sin(&info->ai_addr)->sin_addr, hr_addr, sizeof(hr_addr)));

  k_poll_signal_raise(&dnsFinishedSignal, 1);
}

static int setup_socket(sa_family_t family, const char *server, int port, int *sock,
                        struct sockaddr *addr, socklen_t addr_len) {
  const char *family_str = "IPv4";
  int ret = 0;

  memset(addr, 0, addr_len);

  ret = dns_get_addr_info(server, DNS_QUERY_TYPE_A, NULL, dns_result_cb, (void *)addr, DNS_TIMEOUT);
  if (ret < 0) {
    LOG_ERR("Cannot resolve IPv4 address (%d)", ret);
    return ret;
  }

  k_poll_signal_init(&dnsFinishedSignal);
  k_poll_event_init(dnsFinishedEvents, K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY,
                    &dnsFinishedSignal);
  k_poll(dnsFinishedEvents, ARRAY_SIZE(dnsFinishedEvents), K_SECONDS(5));

  net_sin(addr)->sin_port = htons(port);

  LOG_HEXDUMP_INF(addr, sizeof(struct sockaddr), "Address:");

  if (IS_ENABLED(CONFIG_NET_SOCKETS_SOCKOPT_TLS)) {
    sec_tag_t sec_tag_list[] = {
        CA_CERTIFICATE_TAG,
    };

    int peer_verify = TLS_PEER_VERIFY_REQUIRED;

    size_t credlen = 0; // We just want to know if the certificate exists, 
                        // a size of null should return -EFBIG without touching the buffer.
    ret = tls_credential_get(CA_CERTIFICATE_TAG, TLS_CREDENTIAL_CA_CERTIFICATE, NULL, &credlen);
    if(ret == -ENOENT) {
      ret = tls_credential_add(CA_CERTIFICATE_TAG, TLS_CREDENTIAL_CA_CERTIFICATE, ca_certificate,
                              sizeof(ca_certificate));
      if (ret < 0) {
        LOG_ERR("Failed to register public certificate: %d", ret);
        return ret;
      }
    }
  

    *sock = socket(family, SOCK_STREAM, IPPROTO_TLS_1_2);
    if (*sock >= 0) {
      ret = setsockopt(*sock, SOL_TLS, TLS_SEC_TAG_LIST, sec_tag_list, sizeof(sec_tag_list));
      if (ret < 0) {
        LOG_ERR("Failed to set %s secure option (%d)", family_str, -errno);
        ret = -errno;
      }

      ret = setsockopt(*sock, SOL_TLS, TLS_HOSTNAME, OPENAI_API_HOST, sizeof(OPENAI_API_HOST));
      if (ret < 0) {
        LOG_ERR(
            "Failed to set %s TLS_HOSTNAME "
            "option (%d)",
            family_str, -errno);
        ret = -errno;
      }

      ret = setsockopt(*sock, SOL_TLS, TLS_PEER_VERIFY, &peer_verify, sizeof(peer_verify));
      if (ret < 0) {
        LOG_ERR(
            "Failed to set %s TLS_PEER_VERIFY "
            "option (%d)",
            family_str, -errno);
        ret = -errno;
      }
    }
  } else {
    *sock = socket(family, SOCK_STREAM, IPPROTO_TCP);
  }

  if (*sock < 0) {
    LOG_ERR("Failed to create %s HTTP socket (%d)", family_str, -errno);
  }

  return ret;
}

int connect_socket(sa_family_t family, const char *server, int port, int *sock,
                   struct sockaddr *addr, socklen_t addr_len) {
  int ret;

  ret = setup_socket(family, server, port, sock, addr, addr_len);
  if (ret < 0 || *sock < 0) {
    return -1;
  }

  ret = connect(*sock, addr, addr_len);
  if (ret < 0) {
    LOG_ERR("Cannot connect to %s remote (%d)", family == AF_INET ? "IPv4" : "IPv6", -errno);
    ret = -errno;
  }

  return ret;
}