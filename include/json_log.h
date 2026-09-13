#ifndef JSON_LOG_H
#define JSON_LOG_H

#include "trace_context.h"

#include <stddef.h>
#include <stdint.h>

#define JSON_LOG_SCHEMA "shp.telemetry.v1"
#define JSON_LOG_MAX_EVENT 48U
#define JSON_LOG_MAX_BACKEND 96U
#define JSON_LOG_MAX_RESULT 48U
#define JSON_LOG_MAX_MESSAGE 256U
#define JSON_LOG_MAX_LINE 2048U

typedef struct {
    uint64_t timestamp_unix_ns;
    const char *event;
    const char *backend;
    const char *result;
    const char *message;
    unsigned attempt;
    traceparent_source_t trace_source;
    const trace_context_t *trace;
} json_log_event_t;

/*
 * Serialize one bounded, single-line JSON telemetry event.
 * Returns encoded byte length (excluding NUL), or -1 with errno set.
 */
int json_log_serialize(const json_log_event_t *event,
                       char *out,
                       size_t out_len);

/* Append a JSON string value, including quotes, with RFC 8259 escaping. */
int json_log_escape_string(const char *input,
                           char *out,
                           size_t out_len,
                           size_t *written_out);

uint64_t json_log_now_unix_ns(void);

#endif
