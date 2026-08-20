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

    io_buffer_init(&connection->input);

    connection->event_source.kind =
        EVENT_SOURCE_CLIENT;

    connection->event_source.object =
        connection;

    return connection;
}

void connection_free(
    connection_t *connection
)
{
    free(connection);
}
