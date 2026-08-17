#include "io_buffer.h"

#include <assert.h>
#include <string.h>

void io_buffer_init(io_buffer_t *buffer)
{
    assert(buffer != NULL);

    buffer->start = 0;
    buffer->len = 0;
}

size_t io_buffer_size(const io_buffer_t *buffer)
{
    assert(buffer != NULL);

    return buffer->len;
}

size_t io_buffer_tail_space(const io_buffer_t *buffer)
{
    assert(buffer != NULL);
    assert(buffer->start + buffer->len
           <= IO_BUFFER_CAPACITY);

    return IO_BUFFER_CAPACITY
           - buffer->start
           - buffer->len;
}

unsigned char *io_buffer_write_ptr(io_buffer_t *buffer)
{
    assert(buffer != NULL);

    return buffer->data
           + buffer->start
           + buffer->len;
}

const unsigned char *io_buffer_read_ptr(
    const io_buffer_t *buffer
)
{
    assert(buffer != NULL);

    return buffer->data + buffer->start;
}

void io_buffer_produce(
    io_buffer_t *buffer,
    size_t count
)
{
    assert(buffer != NULL);
    assert(count <= io_buffer_tail_space(buffer));

    buffer->len += count;
}

void io_buffer_consume(
    io_buffer_t *buffer,
    size_t count
)
{
    assert(buffer != NULL);
    assert(count <= buffer->len);

    buffer->start += count;
    buffer->len -= count;

    if (buffer->len == 0) {
        buffer->start = 0;
    }
}

void io_buffer_compact(io_buffer_t *buffer)
{
    assert(buffer != NULL);

    if (buffer->start == 0) {
        return;
    }

    if (buffer->len > 0) {
        memmove(
            buffer->data,
            buffer->data + buffer->start,
            buffer->len
        );
    }

    buffer->start = 0;
}
