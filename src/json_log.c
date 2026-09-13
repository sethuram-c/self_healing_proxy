#define _POSIX_C_SOURCE 200809L
#include "json_log.h"
#include "ipc_frame.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct {
    char *buf;
    size_t cap;
    size_t used;
} writer_t;

static int writer_putc(writer_t *w, char ch)
{
    if (w == NULL || w->buf == NULL || w->cap == 0U || w->used + 1U >= w->cap) {
        errno = ENOSPC;
        return -1;
    }
    w->buf[w->used++] = ch;
    w->buf[w->used] = '\0';
    return 0;
}

static int writer_raw(writer_t *w, const char *text)
{
    size_t len;

    if (w == NULL || text == NULL) {
        errno = EINVAL;
        return -1;
    }
    len = strlen(text);
    if (w->used + len >= w->cap) {
        errno = ENOSPC;
        return -1;
    }
    memcpy(w->buf + w->used, text, len);
    w->used += len;
    w->buf[w->used] = '\0';
    return 0;
}

static int writer_printf(writer_t *w, const char *fmt, ...)
{
    va_list ap;
    int n;

    if (w == NULL || fmt == NULL || w->used >= w->cap) {
        errno = EINVAL;
        return -1;
    }

    va_start(ap, fmt);
    n = vsnprintf(w->buf + w->used, w->cap - w->used, fmt, ap);
    va_end(ap);

    if (n < 0) {
        return -1;
    }
    if ((size_t)n >= w->cap - w->used) {
        errno = ENOSPC;
        return -1;
    }
    w->used += (size_t)n;
    return 0;
}

static int writer_json_string(writer_t *w, const char *input, size_t max_input)
{
    size_t i;
    size_t len;

    if (w == NULL || input == NULL) {
        errno = EINVAL;
        return -1;
    }

    len = strnlen(input, max_input + 1U);
    if (len > max_input) {
        errno = E2BIG;
        return -1;
    }
    if (writer_putc(w, '"') < 0) {
        return -1;
    }

    for (i = 0U; i < len; ++i) {
        unsigned char ch = (unsigned char)input[i];
        switch (ch) {
        case '"':
            if (writer_raw(w, "\\\"") < 0) return -1;
            break;
        case '\\':
            if (writer_raw(w, "\\\\") < 0) return -1;
            break;
        case '\b':
            if (writer_raw(w, "\\b") < 0) return -1;
            break;
        case '\f':
            if (writer_raw(w, "\\f") < 0) return -1;
            break;
        case '\n':
            if (writer_raw(w, "\\n") < 0) return -1;
            break;
        case '\r':
            if (writer_raw(w, "\\r") < 0) return -1;
            break;
        case '\t':
            if (writer_raw(w, "\\t") < 0) return -1;
            break;
        default:
            if (ch < 0x20U) {
                if (writer_printf(w, "\\u%04x", (unsigned)ch) < 0) return -1;
            } else {
                if (writer_putc(w, (char)ch) < 0) return -1;
            }
            break;
        }
    }

    return writer_putc(w, '"');
}

int json_log_escape_string(const char *input,
                           char *out,
                           size_t out_len,
                           size_t *written_out)
{
    writer_t w;

    if (input == NULL || out == NULL || out_len == 0U) {
        errno = EINVAL;
        return -1;
    }

    w.buf = out;
    w.cap = out_len;
    w.used = 0U;
    out[0] = '\0';

    if (writer_json_string(&w, input, JSON_LOG_MAX_MESSAGE) < 0) {
        return -1;
    }
    if (written_out != NULL) {
        *written_out = w.used;
    }
    return 0;
}

uint64_t json_log_now_unix_ns(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return 0U;
    }
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
}

int json_log_serialize(const json_log_event_t *event,
                       char *out,
                       size_t out_len)
{
    writer_t w;
    char request_hex[REQUEST_ID_HEX_LEN + 1U];
    char trace_hex[TRACE_ID_HEX_LEN + 1U];
    char span_hex[SPAN_ID_HEX_LEN + 1U];
    const char *backend;
    const char *result;
    const char *message;

    if (event == NULL || out == NULL || out_len == 0U || event->trace == NULL ||
        event->event == NULL || event->timestamp_unix_ns == 0U || event->attempt == 0U) {
        errno = EINVAL;
        return -1;
    }

    if (trace_context_request_id_hex(event->trace, request_hex, sizeof(request_hex)) < 0 ||
        trace_context_trace_id_hex(event->trace, trace_hex, sizeof(trace_hex)) < 0 ||
        trace_context_span_id_hex(event->trace, span_hex, sizeof(span_hex)) < 0) {
        return -1;
    }

    backend = event->backend != NULL ? event->backend : "";
    result = event->result != NULL ? event->result : "";
    message = event->message != NULL ? event->message : "";

    w.buf = out;
    w.cap = out_len;
    w.used = 0U;
    out[0] = '\0';

    if (writer_raw(&w, "{\"schema\":\"") < 0 ||
        writer_raw(&w, JSON_LOG_SCHEMA) < 0 ||
        writer_raw(&w, "\",\"timestamp_unix_ns\":") < 0 ||
        writer_printf(&w, "%llu", (unsigned long long)event->timestamp_unix_ns) < 0 ||
        writer_raw(&w, ",\"event\":") < 0 ||
        writer_json_string(&w, event->event, JSON_LOG_MAX_EVENT) < 0 ||
        writer_raw(&w, ",\"request_id\":\"") < 0 ||
        writer_raw(&w, request_hex) < 0 ||
        writer_raw(&w, "\",\"trace_id\":\"") < 0 ||
        writer_raw(&w, trace_hex) < 0 ||
        writer_raw(&w, "\",\"span_id\":\"") < 0 ||
        writer_raw(&w, span_hex) < 0 ||
        writer_raw(&w, "\",\"trace_source\":") < 0 ||
        writer_json_string(&w, traceparent_source_name(event->trace_source), JSON_LOG_MAX_RESULT) < 0 ||
        writer_raw(&w, ",\"attempt\":") < 0 ||
        writer_printf(&w, "%u", event->attempt) < 0 ||
        writer_raw(&w, ",\"backend\":") < 0 ||
        writer_json_string(&w, backend, JSON_LOG_MAX_BACKEND) < 0 ||
        writer_raw(&w, ",\"result\":") < 0 ||
        writer_json_string(&w, result, JSON_LOG_MAX_RESULT) < 0 ||
        writer_raw(&w, ",\"message\":") < 0 ||
        writer_json_string(&w, message, JSON_LOG_MAX_MESSAGE) < 0 ||
        writer_putc(&w, '}') < 0) {
        return -1;
    }

    if (w.used >= IPC_FRAME_MAX_PAYLOAD) {
        errno = EMSGSIZE;
        return -1;
    }
    return (int)w.used;
}
