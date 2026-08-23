#ifndef HTTP_FRAMING_H
#define HTTP_FRAMING_H

#include <stddef.h>

#define HTTP_MAX_HEADER_BYTES 4096

typedef enum {
    HTTP_FRAME_INCOMPLETE = 0,
    HTTP_FRAME_COMPLETE = 1,
    HTTP_FRAME_ERROR = -1
} http_frame_result_t;

typedef struct {
    size_t header_bytes;
    size_t content_length;
    size_t total_bytes;
} http_request_frame_t;

http_frame_result_t http_request_frame_inspect(
    const unsigned char *data,
    size_t length,
    http_request_frame_t *frame
);

#endif
