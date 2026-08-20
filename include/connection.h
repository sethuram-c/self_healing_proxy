#ifndef CONNECTION_H
#define CONNECTION_H

#include "io_buffer.h"

#include <stdint.h>
#include <stddef.h>

typedef enum {
    EVENT_SOURCE_LISTENER = 1,
    EVENT_SOURCE_CLIENT = 2
} event_source_kind_t;

typedef struct {
    event_source_kind_t kind;
    void *object;
} event_source_t;

typedef struct connection {
    uint64_t id;
    int fd;

    io_buffer_t input;

    size_t bytes_received;

    event_source_t event_source;

    struct connection *prev;
    struct connection *next;
} connection_t;

connection_t *connection_create(
    int fd,
    uint64_t id
);

void connection_free(connection_t *connection);

#endif
