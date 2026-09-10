#include "ipc_frame.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void store_u32_be(uint8_t *dst, uint32_t value)
{
    uint32_t net = htonl(value);
    memcpy(dst, &net, sizeof(net));
}

static uint32_t load_u32_be(const uint8_t *src)
{
    uint32_t net = 0;
    memcpy(&net, src, sizeof(net));
    return ntohl(net);
}

static uint16_t load_u16_be(const uint8_t *src)
{
    uint16_t net = 0;
    memcpy(&net, src, sizeof(net));
    return ntohs(net);
}

void ipc_frame_decoder_init(ipc_frame_decoder_t *decoder)
{
    if (decoder == NULL) {
        return;
    }

    memset(decoder, 0, sizeof(*decoder));
}

int ipc_frame_type_valid(uint8_t type)
{
    return type == IPC_FRAME_TYPE_EVENT ||
           type == IPC_FRAME_TYPE_CONTROL ||
           type == IPC_FRAME_TYPE_HEARTBEAT;
}

int ipc_frame_encode_header(uint8_t type,
                            uint32_t payload_len,
                            uint8_t out[IPC_FRAME_HEADER_SIZE])
{
    if (out == NULL || !ipc_frame_type_valid(type) ||
        payload_len > IPC_FRAME_MAX_PAYLOAD) {
        errno = EINVAL;
        return -1;
    }

    store_u32_be(out + 0, IPC_FRAME_MAGIC);
    out[4] = IPC_FRAME_VERSION;
    out[5] = type;
    out[6] = 0;
    out[7] = 0;
    store_u32_be(out + 8, payload_len);
    return 0;
}

static int send_all(int fd, const uint8_t *buf, size_t len)
{
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return -1;
    }

    return 0;
}

int ipc_frame_send(int fd,
                   uint8_t type,
                   const void *payload,
                   uint32_t payload_len)
{
    uint8_t header[IPC_FRAME_HEADER_SIZE];

    if (fd < 0 || (payload_len > 0 && payload == NULL)) {
        errno = EINVAL;
        return -1;
    }

    if (ipc_frame_encode_header(type, payload_len, header) < 0) {
        return -1;
    }

    if (send_all(fd, header, sizeof(header)) < 0) {
        return -1;
    }

    if (payload_len > 0 &&
        send_all(fd, (const uint8_t *)payload, payload_len) < 0) {
        return -1;
    }

    return 0;
}

static int parse_header(ipc_frame_decoder_t *decoder)
{
    uint32_t magic = load_u32_be(decoder->header + 0);
    uint8_t version = decoder->header[4];
    uint8_t type = decoder->header[5];
    uint16_t flags = load_u16_be(decoder->header + 6);
    uint32_t payload_len = load_u32_be(decoder->header + 8);

    if (magic != IPC_FRAME_MAGIC) {
        errno = EPROTO;
        return -1;
    }
    if (version != IPC_FRAME_VERSION) {
        errno = EPROTONOSUPPORT;
        return -1;
    }
    if (!ipc_frame_type_valid(type) || flags != 0) {
        errno = EPROTO;
        return -1;
    }
    if (payload_len > IPC_FRAME_MAX_PAYLOAD) {
        errno = EMSGSIZE;
        return -1;
    }

    decoder->frame_type = type;
    decoder->payload_len = payload_len;
    decoder->payload_used = 0;
    return 0;
}

static void reset_current_frame(ipc_frame_decoder_t *decoder)
{
    decoder->header_used = 0;
    decoder->payload_len = 0;
    decoder->payload_used = 0;
    decoder->frame_type = 0;
}

int ipc_frame_decoder_feed(ipc_frame_decoder_t *decoder,
                           const uint8_t *data,
                           size_t data_len,
                           ipc_frame_callback callback,
                           void *ctx)
{
    size_t off = 0;

    if (decoder == NULL || callback == NULL ||
        (data_len > 0 && data == NULL)) {
        errno = EINVAL;
        return -1;
    }

    while (off < data_len) {
        if (decoder->header_used < IPC_FRAME_HEADER_SIZE) {
            size_t need = IPC_FRAME_HEADER_SIZE - decoder->header_used;
            size_t take = data_len - off < need ? data_len - off : need;

            memcpy(decoder->header + decoder->header_used, data + off, take);
            decoder->header_used += take;
            off += take;

            if (decoder->header_used < IPC_FRAME_HEADER_SIZE) {
                continue;
            }

            if (parse_header(decoder) < 0) {
                return -1;
            }

            if (decoder->payload_len == 0) {
                if (callback(decoder->frame_type, NULL, 0, ctx) != 0) {
                    errno = ECANCELED;
                    return -1;
                }
                reset_current_frame(decoder);
                continue;
            }
        }

        if (decoder->payload_used < decoder->payload_len) {
            uint32_t need = decoder->payload_len - decoder->payload_used;
            size_t available = data_len - off;
            size_t take = available < (size_t)need ? available : (size_t)need;

            memcpy(decoder->payload + decoder->payload_used, data + off, take);
            decoder->payload_used += (uint32_t)take;
            off += take;
        }

        if (decoder->payload_used == decoder->payload_len) {
            if (callback(decoder->frame_type,
                         decoder->payload,
                         decoder->payload_len,
                         ctx) != 0) {
                errno = ECANCELED;
                return -1;
            }
            reset_current_frame(decoder);
        }
    }

    return 0;
}
