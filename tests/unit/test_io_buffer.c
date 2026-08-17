#include "io_buffer.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_initial_state(void)
{
    io_buffer_t buffer;

    io_buffer_init(&buffer);

    assert(io_buffer_size(&buffer) == 0);
    assert(buffer.start == 0);

    assert(
        io_buffer_tail_space(&buffer)
        == IO_BUFFER_CAPACITY
    );
}

static void test_produce_and_consume(void)
{
    io_buffer_t buffer;

    io_buffer_init(&buffer);

    const char payload[] = "abcdef";

    memcpy(
        io_buffer_write_ptr(&buffer),
        payload,
        sizeof(payload) - 1
    );

    io_buffer_produce(
        &buffer,
        sizeof(payload) - 1
    );

    assert(io_buffer_size(&buffer) == 6);

    io_buffer_consume(&buffer, 2);

    assert(io_buffer_size(&buffer) == 4);
    assert(buffer.start == 2);

    assert(
        memcmp(
            io_buffer_read_ptr(&buffer),
            "cdef",
            4
        ) == 0
    );
}

static void test_empty_buffer_resets_start(void)
{
    io_buffer_t buffer;

    io_buffer_init(&buffer);

    memcpy(
        io_buffer_write_ptr(&buffer),
        "hello",
        5
    );

    io_buffer_produce(&buffer, 5);
    io_buffer_consume(&buffer, 5);

    assert(buffer.start == 0);
    assert(buffer.len == 0);
}

static void test_compaction(void)
{
    io_buffer_t buffer;

    io_buffer_init(&buffer);

    memcpy(
        io_buffer_write_ptr(&buffer),
        "abcdefgh",
        8
    );

    io_buffer_produce(&buffer, 8);
    io_buffer_consume(&buffer, 3);

    assert(buffer.start == 3);
    assert(buffer.len == 5);

    io_buffer_compact(&buffer);

    assert(buffer.start == 0);
    assert(buffer.len == 5);

    assert(
        memcmp(
            io_buffer_read_ptr(&buffer),
            "defgh",
            5
        ) == 0
    );
}

int main(void)
{
    test_initial_state();
    test_produce_and_consume();
    test_empty_buffer_resets_start();
    test_compaction();

    puts("io_buffer tests passed");

    return 0;
}
