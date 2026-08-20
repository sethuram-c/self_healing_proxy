#include "connection.h"

#include <assert.h>
#include <stdio.h>


int main(void)
{
    connection_t *connection =
        connection_create(
            42,
            1001
        );

    assert(connection != NULL);

    assert(connection->fd == 42);
    assert(connection->id == 1001);

    assert(
        connection->bytes_received == 0
    );

    assert(
        io_buffer_size(
            &connection->input
        ) == 0
    );

    assert(
        connection->event_source.kind
        == EVENT_SOURCE_CLIENT
    );

    assert(
        connection->event_source.object
        == connection
    );

    assert(connection->prev == NULL);
    assert(connection->next == NULL);

    connection_free(connection);

    puts(
        "connection state tests passed"
    );

    return 0;
}
