#include "connection.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    connection_t *connection =
        connection_create(42, 1001);

    assert(connection != NULL);

    assert(connection->fd == 42);
    assert(connection->id == 1001);
    assert(connection->backend_fd == -1);
    assert(connection->backend_index == 0);
    assert(connection->backend_selected == 0);

    assert(
        io_buffer_size(
            &connection->client_to_backend
        ) == 0
    );

    assert(
        io_buffer_size(
            &connection->backend_to_client
        ) == 0
    );

    assert(connection->client_bytes_ready == 0);
    assert(connection->bytes_received == 0);
    assert(connection->backend_bytes_received == 0);
    assert(connection->bytes_forwarded_to_backend == 0);
    assert(connection->bytes_forwarded_to_client == 0);
    assert(connection->requests_received == 0);

    assert(connection->client_read_eof == 0);
    assert(connection->backend_read_eof == 0);

    assert(connection->client_read_paused == 0);
    assert(connection->backend_read_paused == 0);
    assert(connection->client_pause_count == 0);
    assert(connection->client_resume_count == 0);
    assert(connection->backend_pause_count == 0);
    assert(connection->backend_resume_count == 0);

    assert(connection->accepted_at_ms == 0);
    assert(connection->last_activity_ms == 0);
    assert(connection->request_started_at_ms == 0);
    assert(connection->backend_connect_started_at_ms == 0);
    assert(connection->response_wait_started_at_ms == 0);

    assert(
        connection->backend_state
        == BACKEND_NOT_STARTED
    );

    assert(
        connection->event_source.kind
        == EVENT_SOURCE_CLIENT
    );
    assert(
        connection->event_source.object
        == connection
    );

    assert(
        connection->backend_event_source.kind
        == EVENT_SOURCE_BACKEND
    );
    assert(
        connection->backend_event_source.object
        == connection
    );

    assert(connection->destroyed == 0);
    assert(connection->prev == NULL);
    assert(connection->next == NULL);
    assert(connection->garbage_next == NULL);

    connection_free(connection);

    puts("connection state tests passed");

    return 0;
}
