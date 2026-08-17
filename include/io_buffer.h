#ifndef IO_BUFFER_H
#define IO_BUFFER_H

#include <stddef.h>

#define IO_BUFFER_CAPACITY 4096

typedef struct {
    unsigned char data[IO_BUFFER_CAPACITY];
    size_t start;
    size_t len;
} io_buffer_t;

void io_buffer_init(io_buffer_t *buffer);

size_t io_buffer_size(const io_buffer_t *buffer);

size_t io_buffer_tail_space(const io_buffer_t *buffer);

unsigned char *io_buffer_write_ptr(io_buffer_t *buffer);

const unsigned char *io_buffer_read_ptr(
    const io_buffer_t *buffer
);

void io_buffer_produce(
    io_buffer_t *buffer,
    size_t count
);

void io_buffer_consume(
    io_buffer_t *buffer,
    size_t count
);

void io_buffer_compact(io_buffer_t *buffer);

#endif
