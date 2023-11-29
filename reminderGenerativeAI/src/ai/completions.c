#include <zephyr/logging/log.h>

#include "socket_common.h"
LOG_MODULE_REGISTER(completions, CONFIG_MAIN_LOG_LEVEL);

#include <zephyr/net/http/client.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>

#include <stdio.h>

extern uint8_t recv_buf_ipv4[MAX_RECV_BUF_LEN];
extern uint8_t send_buf_ipv4[MAX_SEND_BUF_LEN];
extern char response_buf[MAX_EXPECTED_RESPONSE_BODY];

/*
curl https://api.openai.com/v1/chat/completions \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer $OPENAI_API_KEY" \
  -d '{
  "model": "gpt-3.5-turbo",
  "messages": [
      {
      "role": "system",
      "content": "You are a poetic assistant, skilled in explaining complex programming concepts with creative flair."
      },
      {
      "role": "user",
      "content": "Compose a poem that explains the concept of recursion in programming."
      }
  ],
  "temperature": 0,
  "max_tokens": 1024
}'

RESPONSE
{
  "id": "chatcmpl-123",
  "object": "chat.completion",
  "created": 1677652288,
  "model": "gpt-3.5-turbo-0613",
  "choices": [{
    "index": 0,
    "message": {
      "role": "assistant",
      "content": "\n\nHello there, how may I assist you today?",
    },
    "finish_reason": "stop"
  }],
  "usage": {
    "prompt_tokens": 9,
    "completion_tokens": 12,
    "total_tokens": 21
  }
}

*/

static const char *headers[] = {
    "Authorization: Bearer " OPENAI_API_KEY NEWLINE,
    "Content-Type: application/json" NEWLINE,
    NULL
};

static const char* post_data_fmt =
    "{"
        "\"model\": \"gpt-3.5-turbo\","
        "\"messages\": ["
            "{"
            "\"role\": \"system\","
            "\"content\": \""
                "This is a reminder app.\\n"
                "It is your task to interpret the users' requests to add or delete reminders.\\n"
                "\\n"
                "A reminder consists of a name and a due date.\\n"
                "The format of the due date is \\\"MM-DD hh:mm\\\"\\\"\\n"
                "\\n"
                "Now is \\\"11-28 18:54\\\".\\n"
                "\\n"
                "\\n"
                "Examples of reminders:\\n"
                "{\\\"name\\\": \\\"homework\\\", \\\"due\\\": \\\"11-29 15:00\\\"}\\n"
                "{\\\"name\\\": \\\"dinner\\\", \\\"due\\\": \\\"11-29 18:00\\\"}\\n"
                "{\\\"name\\\": \\\"go to bed\\\", \\\"due\\\": \\\"11-28 21:30\\\"}\\n"
                "\\n"
                "Both name and due are mandatory. Each reminder must have a name and a due date.\\n"
                "\\n"
                "Please classify the request into one of \\\"delete\\\" or \\\"add\\\".\\n"
                "Then identify the parameters.\\n"
                "\\n"
                "These are the current active reminders:\\n"
                "{\\\"name\\\": \\\"homework\\\", \\\"due\\\": \\\"11-29 15:00\\\"}\\n"
                "{\\\"name\\\": \\\"dinner\\\", \\\"due\\\": \\\"11-29 18:00\\\"}\\n"
                "\\n"
                "\\n"
                "Examples of requests with your responses:\\n"
                "\\n"
                "Request: \\\"I finished my homework.\\\"\\n"
                "Response: {\\\"request\\\": \\\"delete\\\", \\\"parameter\\\": {\\\"name\\\": \\\"homework\\\"}}\\n"
                "\\n"
                "Request: \\\"I finished dinner.\\\"\\n"
                "Response: {\\\"request\\\": \\\"delete\\\", \\\"parameter\\\": {\\\"name\\\": \\\"dinner\\\"}}\\n"
                "\\n"
                "Request: \\\"Delete the homework reminder.\\\"\\n"
                "Response: {\\\"request\\\": \\\"delete\\\", \\\"parameter\\\": {\\\"name\\\": \\\"homework\\\"}}\\n"
                "\\n"
                "Request: \\\"Add homework with a due date of 3 p.m.\\\"\\n"
                "Response: {\\\"request\\\": \\\"add\\\", \\\"parameter\\\": {\\\"name\\\": \\\"homework\\\", \\\"due\\\": \\\"11-29 15:00\\\"}}\\n"
                "\\n"
                "Request: \\\"Add dinner at 6 p.m.\\\"\\n"
                "Response: {\\\"request\\\": \\\"add\\\", \\\"parameter\\\": {\\\"name\\\": \\\"dinner\\\", \\\"due\\\": \\\"11-29 18:00\\\"}}\\n"
                "\\n"
                "Request: \\\"Add getting up at 6 a.m.\\\"\\n"
                "Response: {\\\"request\\\": \\\"add\\\", \\\"parameter\\\": {\\\"name\\\": \\\"getting up\\\", \\\"due\\\": \\\"11-29 06:00\\\"}}\\n"
                "\\n"
                "Request: \\\"Can you add the meeting at 2:30 p.m.?\\\"\\n"
                "Response: {\\\"request\\\": \\\"add\\\", \\\"parameter\\\": {\\\"name\\\": \\\"meeting\\\", \\\"due\\\": \\\"11-28 14:30\\\"}}\\n"
                "\\n"
                "Request: \\\"Please delete the go to bed reminder.\\\"\\n"
                "Response: {\\\"request\\\": \\\"delete\\\", \\\"parameter\\\": {\\\"name\\\": \\\"go to bed\\\"}}\\n"
                "\\n"
                "\\n"
                "This is the request:\\n"
              "\""         
            "},"
            "{"
            "\"role\": \"user\","
            "\"content\": \"%s\""
            "}"
        "],"
        "\"temperature\": 0,"
        "\"max_tokens\": 1024"
    "}";


#include "cJSON.h"

static void parse_json(const char *data, uint16_t len) {
  const cJSON *choice = NULL;
    
  // get root["choices"][0]["message"]["content"]
  cJSON *jsonRoot = cJSON_Parse(data);
  if (!cJSON_IsObject(jsonRoot)) {
    const char *error_ptr = cJSON_GetErrorPtr();
    if (error_ptr != NULL) {
      LOG_ERR("Error before: %s", error_ptr);
    }
    goto end;
  }
  cJSON *jsonChoices = cJSON_GetObjectItemCaseSensitive(jsonRoot, "choices");
  if (!cJSON_IsArray(jsonChoices)) {
    LOG_ERR("No choices available");
    goto end;
  }
  cJSON_ArrayForEach(choice, jsonChoices) {
    cJSON *jsonMessage = cJSON_GetObjectItemCaseSensitive(choice, "message");
    if (!cJSON_IsObject(jsonMessage)) {
      LOG_ERR("No message available");
      goto end;
    }
    cJSON *jsonContent = cJSON_GetObjectItemCaseSensitive(jsonMessage, "content");
    if (!cJSON_IsString(jsonContent)) {
      LOG_ERR("No content available");
      goto end;
    }
    const char *contentString = jsonContent->valuestring;

    memset(response_buf, 0, MAX_EXPECTED_RESPONSE_BODY);
    strncpy(response_buf, contentString, MAX_EXPECTED_RESPONSE_BODY);
    LOG_INF("chat completion result: %s", response_buf);    
  }

end:
  cJSON_Delete(jsonRoot);
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

    parse_json(rsp->body_frag_start, rsp->body_frag_len);
  }
}

int request_chat_completion(const char *request) {
  struct sockaddr_in addr4;
  int sock4 = -1;
  int32_t timeout = HTTP_REQUEST_TIMEOUT;
  int ret = 0;
  int port = OPENAI_API_PORT;

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

  snprintf(send_buf_ipv4, sizeof(send_buf_ipv4), post_data_fmt, request);

  if (sock4 >= 0 && IS_ENABLED(CONFIG_NET_IPV4)) {
    struct http_request req;
    memset(&req, 0, sizeof(req));

    req.method = HTTP_POST;
    req.url = OPENAI_API_CHAT_COMPLETION_ENDPOINT;
    req.host = OPENAI_API_HOST;
    req.protocol = "HTTP/1.1";
    req.payload = send_buf_ipv4;
    req.payload_len = strlen(send_buf_ipv4);
    req.header_fields = headers;
    req.response = response_cb;
    req.recv_buf = recv_buf_ipv4;
    req.recv_buf_len = sizeof(recv_buf_ipv4);

    ret = http_client_req(sock4, &req, timeout, (void *)request);

    close(sock4);
  }

  return ret;
}
