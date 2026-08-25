#include "connection.h"

#include <stdlib.h>

connection_t *connection_create(
    int fd,
    uint64_t id
)
{
    connection_t *connection =
        calloc(1, sizeof(*connection));

    if (connection == NULL) {
        return NULL;
    }

    connection->id = id;
    connection->fd = fd;
    connection->backend_fd = -1;

    io_buffer_init(&connection->client_to_backend);
    io_buffer_init(&connection->backend_to_client);

    connection->backend_state =
        BACKEND_NOT_STARTED;

    connection->event_source.kind =
        EVENT_SOURCE_CLIENT;
    connection->event_source.object =
        connection;

    connection->backend_event_source.kind =
        EVENT_SOURCE_BACKEND;
    connection->backend_event_source.object =
        connection;

    return connection;
}

void connection_free(
    connection_t *connection
)
{
    free(connection);
}
