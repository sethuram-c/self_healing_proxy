#ifndef CONNECTION_H
#define CONNECTION_H

#include "io_buffer.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
    EVENT_SOURCE_LISTENER = 1,
    EVENT_SOURCE_CLIENT = 2,
    EVENT_SOURCE_BACKEND = 3
} event_source_kind_t;

typedef enum {
    BACKEND_NOT_STARTED = 0,
    BACKEND_CONNECTING,
    BACKEND_CONNECTED,
    BACKEND_CLOSED,
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

    io_buffer_t client_to_backend;
    io_buffer_t backend_to_client;

    size_t client_bytes_ready;

    size_t bytes_received;
    size_t backend_bytes_received;
    size_t bytes_forwarded_to_backend;
    size_t bytes_forwarded_to_client;
    size_t requests_received;

    int client_read_eof;
    int backend_read_eof;

    int client_read_paused;
    int backend_read_paused;

    size_t client_pause_count;
    size_t client_resume_count;
    size_t backend_pause_count;
    size_t backend_resume_count;

    uint64_t accepted_at_ms;
    uint64_t last_activity_ms;
    uint64_t request_started_at_ms;
    uint64_t backend_connect_started_at_ms;
    uint64_t response_wait_started_at_ms;

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
