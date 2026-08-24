#ifndef NET_UTILS_H
#define NET_UTILS_H

#include <stdint.h>


typedef enum {
    NONBLOCK_CONNECT_ERROR = -1,
    NONBLOCK_CONNECT_CONNECTED = 0,
    NONBLOCK_CONNECT_IN_PROGRESS = 1
} nonblock_connect_result_t;


int set_nonblocking(int fd);


nonblock_connect_result_t
start_nonblocking_connect_ipv4(
    const char *address,
    uint16_t port,
    int *fd_out
);


int finish_nonblocking_connect(
    int fd
);


#endif
