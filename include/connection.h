#ifndef CONNECTION_H
#define CONNECTION_H

#include "io_buffer.h"

#include <stdint.h>
#include <stddef.h>

typedef enum {
    EVENT_SOURCE_LISTENER = 1,
    EVENT_SOURCE_CLIENT = 2,
    EVENT_SOURCE_BACKEND = 3
} event_source_kind_t;

typedef enum {
    BACKEND_NOT_STARTED = 0,
    BACKEND_CONNECTING,
    BACKEND_CONNECTED,
    BACKEND_FAILED
} backend_state_t;

typedef struct {
    event_source_kind_t kind;
    void *object;
} event_source_t;

typedef struct connection {
    uint64_t id;

    int fd;
    int backend_fd;

    io_buffer_t input;

    size_t bytes_received;
    size_t requests_received;

    backend_state_t backend_state;

    event_source_t event_source;
    event_source_t backend_event_source;

    int destroyed;

    struct connection *prev;
    struct connection *next;

    struct connection *garbage_next;
} connection_t;

connection_t *connection_create(
        int fd,
        uint64_t id
        );

void connection_free(connection_t *connection);

#endif
