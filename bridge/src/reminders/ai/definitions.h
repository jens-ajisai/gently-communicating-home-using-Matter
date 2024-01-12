#pragma once

#define CA_CERTIFICATE_TAG 1

#define OPENAI_API_HOST "api.openai.com"
#define OPENAI_API_AUDIO_TRANSCRIPTION_ENDPOINT "/v1/audio/transcriptions"
#define OPENAI_API_CHAT_COMPLETION_ENDPOINT "/v1/chat/completions"
#define OPENAI_API_PORT 443
#define OPENAI_API_KEY CONFIG_OPENAI_API_KEY

#define BOUNDARY "Your_Boundary_String"
#define NEWLINE "\r\n"
#define FILENAME "audio.wav"

#define MAX_RECV_BUF_LEN 4096
#define MAX_SEND_BUF_LEN 4096

#define MAX_EXPECTED_RESPONSE_BODY (1024)

#define HTTP_REQUEST_TIMEOUT (10 * MSEC_PER_SEC)