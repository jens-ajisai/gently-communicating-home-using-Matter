#include <zephyr/logging/log.h>

#include "../persistence/persistence.h"
#include "socket_common.h"
LOG_MODULE_REGISTER(whisper, CONFIG_MAIN_LOG_LEVEL);

#include <zephyr/net/http/client.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>

extern uint8_t recv_buf_ipv4[MAX_RECV_BUF_LEN];
extern char response_buf[MAX_EXPECTED_RESPONSE_BODY];

/*
curl --request POST \
  --url https://api.openai.com/v1/audio/transcriptions \
  --header 'Authorization: Bearer $OPENAI_API_KEY' \
  --header 'Content-Type: multipart/form-data' \
  --form file=@rec-24.wav \
  --form model=whisper-1 \
  --form language=de \
  --form response_format=text
*/

static const char *headers[] = {
    "Authorization: Bearer " OPENAI_API_KEY NEWLINE,
    "Content-Type: multipart/form-data; boundary=" BOUNDARY NEWLINE,
    NULL
};

static const char* post_start =
    "--" BOUNDARY NEWLINE 
    "Content-Disposition: form-data; name=\"model\"" NEWLINE NEWLINE "whisper-1" NEWLINE 
    "--" BOUNDARY NEWLINE 
    "Content-Disposition: form-data; name=\"language\"" NEWLINE NEWLINE "en" NEWLINE 
    "--" BOUNDARY NEWLINE 
    "Content-Disposition: form-data; name=\"response_format\"" NEWLINE NEWLINE "text" NEWLINE 
    "--" BOUNDARY NEWLINE
    "Content-Disposition: form-data; name=\"file\"; filename=\"" FILENAME "\"" NEWLINE
    "Content-Type: audio/wav" NEWLINE NEWLINE;

// audio data in between here

static const char* post_end = NEWLINE "--" BOUNDARY "--" NEWLINE;



void fs_read_cb(void *data, uint16_t len_read, void *ctx) {
  int *sock = (int *)ctx;
  send(*sock, data, len_read, 0);
}

static int payload_cb(int sock, struct http_request *req, void *user_data) {
  static uint8_t buf[1600];
  uint16_t sent_bytes = 0;

  sent_bytes += send(sock, post_start, strlen(post_start), 0);
  sent_bytes += fs_readFile((const char *)user_data, buf, sizeof(buf), fs_read_cb, &sock);
  sent_bytes += send(sock, post_end, strlen(post_end), 0);

  LOG_INF("payload_cb: sent %d bytes.", sent_bytes);
  return sent_bytes;
}

static void response_cb(struct http_response *rsp, enum http_final_call final_data,
                        void *user_data) {
  if (final_data == HTTP_DATA_MORE) {
    LOG_ERR("Partial data received (%zd bytes) -- NOT SUPPORTED YET.", rsp->data_len);
  } else if (final_data == HTTP_DATA_FINAL) {
    LOG_INF("All the data received (%zd bytes)", rsp->data_len);
    LOG_INF("Response for request %s", (const char *)user_data);
    LOG_INF("Response status %s", rsp->http_status);
    LOG_INF("Header: %.*s", rsp->data_len - rsp->body_frag_len, rsp->recv_buf);
    LOG_INF("Body: %.*s", rsp->body_frag_len, rsp->body_frag_start);

    uint16_t maxSize = (rsp->body_frag_len > MAX_EXPECTED_RESPONSE_BODY - 1)
                           ? MAX_EXPECTED_RESPONSE_BODY - 1
                           : rsp->body_frag_len;
    memcpy(response_buf, rsp->body_frag_start, maxSize);
    response_buf[maxSize] = 0;
  }
}

int request_transcription(const char *path) {
  struct sockaddr_in addr4;
  int sock4 = -1;
  int32_t timeout = HTTP_REQUEST_TIMEOUT;
  int ret = 0;
  int port = OPENAI_API_PORT;

  LOG_INF("Request transcription for %s.", path);

  if (IS_ENABLED(CONFIG_NET_IPV4)) {
    ret = connect_socket(AF_INET, OPENAI_API_HOST, port, &sock4, (struct sockaddr *)&addr4,
                         sizeof(addr4));
  }

  if (ret < 0) {
    LOG_ERR("Failed to connect to socket: %d", ret);
    return ret;
  }

  if (sock4 < 0) {
    LOG_ERR("Cannot create HTTP connection.");
    return -ECONNABORTED;
  }

  if (sock4 >= 0 && IS_ENABLED(CONFIG_NET_IPV4)) {
    struct http_request req;
    memset(&req, 0, sizeof(req));

    req.method = HTTP_POST;
    req.url = OPENAI_API_AUDIO_TRANSCRIPTION_ENDPOINT;
    req.host = OPENAI_API_HOST;
    req.protocol = "HTTP/1.1";
    req.payload_cb = payload_cb;
    req.payload_len = strlen(post_start) + fs_getFileSize(path) + strlen(post_end);
    req.header_fields = headers;
    req.response = response_cb;
    req.recv_buf = recv_buf_ipv4;
    req.recv_buf_len = sizeof(recv_buf_ipv4);

    ret = http_client_req(sock4, &req, timeout, (void *)path);

    close(sock4);
  }

  return ret;
}
