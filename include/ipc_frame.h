#ifndef IPC_FRAME_H
#define IPC_FRAME_H

#include <stddef.h>
#include <stdint.h>

#define IPC_FRAME_MAGIC 0x53485031u /* "SHP1" */
#define IPC_FRAME_VERSION 1u
#define IPC_FRAME_HEADER_SIZE 12u
#define IPC_FRAME_MAX_PAYLOAD (64u * 1024u)

#define IPC_FRAME_TYPE_EVENT 1u
#define IPC_FRAME_TYPE_CONTROL 2u
#define IPC_FRAME_TYPE_HEARTBEAT 3u

typedef int (*ipc_frame_callback)(uint8_t type,
                                  const uint8_t *payload,
                                  uint32_t payload_len,
                                  void *ctx);

typedef struct ipc_frame_decoder {
    uint8_t header[IPC_FRAME_HEADER_SIZE];
    size_t header_used;
    uint8_t payload[IPC_FRAME_MAX_PAYLOAD];
    uint32_t payload_len;
    uint32_t payload_used;
    uint8_t frame_type;
} ipc_frame_decoder_t;

void ipc_frame_decoder_init(ipc_frame_decoder_t *decoder);

int ipc_frame_type_valid(uint8_t type);

int ipc_frame_encode_header(uint8_t type,
                            uint32_t payload_len,
                            uint8_t out[IPC_FRAME_HEADER_SIZE]);

int ipc_frame_send(int fd,
                   uint8_t type,
                   const void *payload,
                   uint32_t payload_len);

/*
 * Feed any arbitrary byte chunk from a SOCK_STREAM connection.
 * Returns 0 on success, -1 on malformed protocol/callback failure and sets errno.
 * The callback can be invoked zero, one, or many times per feed call.
 */
int ipc_frame_decoder_feed(ipc_frame_decoder_t *decoder,
                           const uint8_t *data,
                           size_t data_len,
                           ipc_frame_callback callback,
                           void *ctx);

#endif
