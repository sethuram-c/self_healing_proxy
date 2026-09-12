#ifndef TRACE_CONTEXT_H
#define TRACE_CONTEXT_H

#include <stddef.h>
#include <stdint.h>

#define TRACE_ID_BYTES 16U
#define SPAN_ID_BYTES 8U
#define REQUEST_ID_BYTES 16U
#define TRACE_ID_HEX_LEN 32U
#define SPAN_ID_HEX_LEN 16U
#define REQUEST_ID_HEX_LEN 32U
#define TRACEPARENT_TEXT_LEN 55U

typedef enum {
    TRACEPARENT_SOURCE_ABSENT = 0,
    TRACEPARENT_SOURCE_ACCEPTED = 1,
    TRACEPARENT_SOURCE_REJECTED = 2
} traceparent_source_t;

typedef struct {
    uint8_t trace_id[TRACE_ID_BYTES];
    uint8_t parent_span_id[SPAN_ID_BYTES];
    uint8_t span_id[SPAN_ID_BYTES];
    uint8_t request_id[REQUEST_ID_BYTES];
    uint8_t trace_flags;
    int has_remote_parent;
} trace_context_t;

int trace_context_start_request(trace_context_t *ctx,
                                const char *incoming_traceparent,
                                traceparent_source_t *source_out);

int trace_context_next_attempt_span(trace_context_t *ctx);

int trace_context_format_traceparent(const trace_context_t *ctx,
                                     char *out,
                                     size_t out_len);

int trace_context_trace_id_hex(const trace_context_t *ctx,
                               char *out,
                               size_t out_len);
int trace_context_span_id_hex(const trace_context_t *ctx,
                              char *out,
                              size_t out_len);
int trace_context_request_id_hex(const trace_context_t *ctx,
                                 char *out,
                                 size_t out_len);
int trace_context_parent_span_id_hex(const trace_context_t *ctx,
                                     char *out,
                                     size_t out_len);

const char *traceparent_source_name(traceparent_source_t source);

#endif
