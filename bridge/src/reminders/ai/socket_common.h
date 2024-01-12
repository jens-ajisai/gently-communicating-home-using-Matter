#pragma once

#include <zephyr/net/socket.h>
#include "definitions.h"

#ifdef __cplusplus
extern "C" {
#endif

int connect_socket(sa_family_t family, const char *server, int port, int *sock,
                   struct sockaddr *addr, socklen_t addr_len);
                   
#ifdef __cplusplus
}
#endif
