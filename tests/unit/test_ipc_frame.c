#include "ipc_frame.h"

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct capture {
    unsigned count;
    uint8_t type[8];
    uint32_t len[8];
    uint8_t payload[8][64];
} capture_t;

static int capture_cb(uint8_t type,
                      const uint8_t *payload,
                      uint32_t payload_len,
                      void *opaque)
{
    capture_t *cap = opaque;
    unsigned i = cap->count;

    assert(i < 8);
    assert(payload_len <= sizeof(cap->payload[i]));
    cap->type[i] = type;
    cap->len[i] = payload_len;
    if (payload_len > 0) {
        memcpy(cap->payload[i], payload, payload_len);
    }
    cap->count++;
    return 0;
}

static size_t make_frame(uint8_t type,
                         const void *payload,
                         uint32_t payload_len,
                         uint8_t *out,
                         size_t out_cap)
{
    assert(out_cap >= IPC_FRAME_HEADER_SIZE + payload_len);
    assert(ipc_frame_encode_header(type, payload_len, out) == 0);
    if (payload_len > 0) {
        memcpy(out + IPC_FRAME_HEADER_SIZE, payload, payload_len);
    }
    return IPC_FRAME_HEADER_SIZE + payload_len;
}

static void test_fragmentation_and_coalescing(void)
{
    ipc_frame_decoder_t decoder;
    capture_t cap = {0};
    uint8_t bytes[256];
    size_t n1 = make_frame(IPC_FRAME_TYPE_EVENT, "alpha", 5,
                           bytes, sizeof(bytes));
    size_t n2 = make_frame(IPC_FRAME_TYPE_CONTROL, "beta", 4,
                           bytes + n1, sizeof(bytes) - n1);
    size_t total = n1 + n2;
    size_t off = 0;
    const size_t chunks[] = {1, 2, 3, 5, 7, 11, 29, 64};
    size_t chunk_index = 0;

    ipc_frame_decoder_init(&decoder);

    while (off < total) {
        size_t take = chunks[chunk_index++ %
                             (sizeof(chunks) / sizeof(chunks[0]))];
        if (take > total - off) {
            take = total - off;
        }
        assert(ipc_frame_decoder_feed(&decoder,
                                      bytes + off,
                                      take,
                                      capture_cb,
                                      &cap) == 0);
        off += take;
    }

    assert(cap.count == 2);
    assert(cap.type[0] == IPC_FRAME_TYPE_EVENT);
    assert(cap.len[0] == 5);
    assert(memcmp(cap.payload[0], "alpha", 5) == 0);
    assert(cap.type[1] == IPC_FRAME_TYPE_CONTROL);
    assert(cap.len[1] == 4);
    assert(memcmp(cap.payload[1], "beta", 4) == 0);
}

static void test_zero_length_and_multiple_in_one_feed(void)
{
    ipc_frame_decoder_t decoder;
    capture_t cap = {0};
    uint8_t bytes[64];
    size_t n1 = make_frame(IPC_FRAME_TYPE_HEARTBEAT, NULL, 0,
                           bytes, sizeof(bytes));
    size_t n2 = make_frame(IPC_FRAME_TYPE_EVENT, "x", 1,
                           bytes + n1, sizeof(bytes) - n1);

    ipc_frame_decoder_init(&decoder);
    assert(ipc_frame_decoder_feed(&decoder,
                                  bytes,
                                  n1 + n2,
                                  capture_cb,
                                  &cap) == 0);
    assert(cap.count == 2);
    assert(cap.type[0] == IPC_FRAME_TYPE_HEARTBEAT);
    assert(cap.len[0] == 0);
    assert(cap.type[1] == IPC_FRAME_TYPE_EVENT);
    assert(cap.len[1] == 1);
}

static void test_malformed_headers(void)
{
    ipc_frame_decoder_t decoder;
    capture_t cap = {0};
    uint8_t h[IPC_FRAME_HEADER_SIZE];
    uint32_t net;

    assert(ipc_frame_encode_header(IPC_FRAME_TYPE_EVENT, 1, h) == 0);
    h[0] ^= 0xff;
    ipc_frame_decoder_init(&decoder);
    assert(ipc_frame_decoder_feed(&decoder, h, sizeof(h), capture_cb, &cap) < 0);
    assert(errno == EPROTO);

    assert(ipc_frame_encode_header(IPC_FRAME_TYPE_EVENT, 1, h) == 0);
    h[4] = 99;
    ipc_frame_decoder_init(&decoder);
    assert(ipc_frame_decoder_feed(&decoder, h, sizeof(h), capture_cb, &cap) < 0);
    assert(errno == EPROTONOSUPPORT);

    assert(ipc_frame_encode_header(IPC_FRAME_TYPE_EVENT, 1, h) == 0);
    h[7] = 1;
    ipc_frame_decoder_init(&decoder);
    assert(ipc_frame_decoder_feed(&decoder, h, sizeof(h), capture_cb, &cap) < 0);
    assert(errno == EPROTO);

    assert(ipc_frame_encode_header(IPC_FRAME_TYPE_EVENT, 1, h) == 0);
    h[5] = 99;
    ipc_frame_decoder_init(&decoder);
    assert(ipc_frame_decoder_feed(&decoder, h, sizeof(h), capture_cb, &cap) < 0);
    assert(errno == EPROTO);

    assert(ipc_frame_encode_header(IPC_FRAME_TYPE_EVENT, 1, h) == 0);
    net = htonl(IPC_FRAME_MAX_PAYLOAD + 1u);
    memcpy(h + 8, &net, sizeof(net));
    ipc_frame_decoder_init(&decoder);
    assert(ipc_frame_decoder_feed(&decoder, h, sizeof(h), capture_cb, &cap) < 0);
    assert(errno == EMSGSIZE);
}

static void test_send_helper(void)
{
    int fds[2];
    uint8_t wire[IPC_FRAME_HEADER_SIZE + 3];
    ssize_t got;

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    assert(ipc_frame_send(fds[0], IPC_FRAME_TYPE_EVENT, "xyz", 3) == 0);

    got = recv(fds[1], wire, sizeof(wire), MSG_WAITALL);
    assert(got == (ssize_t)sizeof(wire));
    assert(wire[0] == 'S' && wire[1] == 'H' && wire[2] == 'P' && wire[3] == '1');
    assert(wire[4] == IPC_FRAME_VERSION);
    assert(wire[5] == IPC_FRAME_TYPE_EVENT);
    assert(wire[6] == 0 && wire[7] == 0);
    assert(memcmp(wire + IPC_FRAME_HEADER_SIZE, "xyz", 3) == 0);

    close(fds[0]);
    close(fds[1]);
}

int main(void)
{
    test_fragmentation_and_coalescing();
    test_zero_length_and_multiple_in_one_feed();
    test_malformed_headers();
    test_send_helper();
    puts("IPC frame tests passed");
    return 0;
}
