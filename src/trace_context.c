#define _GNU_SOURCE
#include "trace_context.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/random.h>

static int is_lower_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

static int hex_nibble(char c, uint8_t *value)
{
    if (c >= '0' && c <= '9') {
        *value = (uint8_t)(c - '0');
        return 0;
    }
    if (c >= 'a' && c <= 'f') {
        *value = (uint8_t)(10 + (c - 'a'));
        return 0;
    }
    return -1;
}

static int parse_hex_bytes(const char *text, size_t text_len, uint8_t *out, size_t out_len)
{
    size_t i;

    if (text == NULL || out == NULL || text_len != out_len * 2U) {
        return -1;
    }

    for (i = 0; i < text_len; ++i) {
        if (!is_lower_hex(text[i])) {
            return -1;
        }
    }

    for (i = 0; i < out_len; ++i) {
        uint8_t hi;
        uint8_t lo;
        if (hex_nibble(text[i * 2U], &hi) < 0 ||
            hex_nibble(text[i * 2U + 1U], &lo) < 0) {
            return -1;
        }
        out[i] = (uint8_t)((hi << 4U) | lo);
    }
    return 0;
}

static int bytes_all_zero(const uint8_t *bytes, size_t len)
{
    size_t i;
    for (i = 0; i < len; ++i) {
        if (bytes[i] != 0U) {
            return 0;
        }
    }
    return 1;
}

static int random_bytes_nonzero(uint8_t *out, size_t len)
{
    for (;;) {
        size_t used = 0;
        while (used < len) {
            ssize_t n = getrandom(out + used, len - used, 0);
            if (n > 0) {
                used += (size_t)n;
                continue;
            }
            if (n < 0 && errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (!bytes_all_zero(out, len)) {
            return 0;
        }
    }
}

static int parse_traceparent_v00(const char *text,
                                 uint8_t trace_id[TRACE_ID_BYTES],
                                 uint8_t parent_id[SPAN_ID_BYTES],
                                 uint8_t *flags_out)
{
    uint8_t flags[1];

    if (text == NULL || strlen(text) != TRACEPARENT_TEXT_LEN) {
        return -1;
    }
    if (text[2] != '-' || text[35] != '-' || text[52] != '-') {
        return -1;
    }
    if (text[0] != '0' || text[1] != '0') {
        return -1;
    }
    if (parse_hex_bytes(text + 3, TRACE_ID_HEX_LEN, trace_id, TRACE_ID_BYTES) < 0 ||
        bytes_all_zero(trace_id, TRACE_ID_BYTES)) {
        return -1;
    }
    if (parse_hex_bytes(text + 36, SPAN_ID_HEX_LEN, parent_id, SPAN_ID_BYTES) < 0 ||
        bytes_all_zero(parent_id, SPAN_ID_BYTES)) {
        return -1;
    }
    if (parse_hex_bytes(text + 53, 2U, flags, 1U) < 0) {
        return -1;
    }
    *flags_out = flags[0];
    return 0;
}

static char hex_digit(unsigned value)
{
    static const char digits[] = "0123456789abcdef";
    return digits[value & 0xFU];
}

static int format_hex(const uint8_t *bytes, size_t len, char *out, size_t out_len)
{
    size_t i;
    if (bytes == NULL || out == NULL || out_len < len * 2U + 1U) {
        errno = EINVAL;
        return -1;
    }
    for (i = 0; i < len; ++i) {
        out[i * 2U] = hex_digit(bytes[i] >> 4U);
        out[i * 2U + 1U] = hex_digit(bytes[i]);
    }
    out[len * 2U] = '\0';
    return 0;
}

int trace_context_start_request(trace_context_t *ctx,
                                const char *incoming_traceparent,
                                traceparent_source_t *source_out)
{
    uint8_t parsed_trace_id[TRACE_ID_BYTES];
    uint8_t parsed_parent_id[SPAN_ID_BYTES];
    uint8_t parsed_flags = 0;
    traceparent_source_t source = TRACEPARENT_SOURCE_ABSENT;

    if (ctx == NULL || source_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(ctx, 0, sizeof(*ctx));

    if (incoming_traceparent != NULL && incoming_traceparent[0] != '\0') {
        if (parse_traceparent_v00(incoming_traceparent,
                                  parsed_trace_id,
                                  parsed_parent_id,
                                  &parsed_flags) == 0) {
            memcpy(ctx->trace_id, parsed_trace_id, TRACE_ID_BYTES);
            memcpy(ctx->parent_span_id, parsed_parent_id, SPAN_ID_BYTES);
            ctx->trace_flags = parsed_flags;
            ctx->has_remote_parent = 1;
            source = TRACEPARENT_SOURCE_ACCEPTED;
        } else {
            source = TRACEPARENT_SOURCE_REJECTED;
        }
    }

    if (!ctx->has_remote_parent) {
        if (random_bytes_nonzero(ctx->trace_id, TRACE_ID_BYTES) < 0) {
            return -1;
        }
        ctx->trace_flags = 0;
    }

    if (random_bytes_nonzero(ctx->request_id, REQUEST_ID_BYTES) < 0 ||
        random_bytes_nonzero(ctx->span_id, SPAN_ID_BYTES) < 0) {
        return -1;
    }

    *source_out = source;
    return 0;
}

int trace_context_next_attempt_span(trace_context_t *ctx)
{
    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }
    return random_bytes_nonzero(ctx->span_id, SPAN_ID_BYTES);
}

int trace_context_format_traceparent(const trace_context_t *ctx, char *out, size_t out_len)
{
    char trace_hex[TRACE_ID_HEX_LEN + 1U];
    char span_hex[SPAN_ID_HEX_LEN + 1U];
    static const char hex[] = "0123456789abcdef";

    if (ctx == NULL || out == NULL || out_len < TRACEPARENT_TEXT_LEN + 1U) {
        errno = EINVAL;
        return -1;
    }
    if (format_hex(ctx->trace_id, TRACE_ID_BYTES, trace_hex, sizeof(trace_hex)) < 0 ||
        format_hex(ctx->span_id, SPAN_ID_BYTES, span_hex, sizeof(span_hex)) < 0) {
        return -1;
    }

    memcpy(out, "00-", 3U);
    memcpy(out + 3U, trace_hex, TRACE_ID_HEX_LEN);
    out[35] = '-';
    memcpy(out + 36U, span_hex, SPAN_ID_HEX_LEN);
    out[52] = '-';
    out[53] = hex[ctx->trace_flags >> 4U];
    out[54] = hex[ctx->trace_flags & 0x0FU];
    out[55] = '\0';
    return 0;
}

int trace_context_trace_id_hex(const trace_context_t *ctx, char *out, size_t out_len)
{
    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }
    return format_hex(ctx->trace_id, TRACE_ID_BYTES, out, out_len);
}

int trace_context_span_id_hex(const trace_context_t *ctx, char *out, size_t out_len)
{
    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }
    return format_hex(ctx->span_id, SPAN_ID_BYTES, out, out_len);
}

int trace_context_request_id_hex(const trace_context_t *ctx, char *out, size_t out_len)
{
    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }
    return format_hex(ctx->request_id, REQUEST_ID_BYTES, out, out_len);
}

int trace_context_parent_span_id_hex(const trace_context_t *ctx, char *out, size_t out_len)
{
    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }
    return format_hex(ctx->parent_span_id, SPAN_ID_BYTES, out, out_len);
}

const char *traceparent_source_name(traceparent_source_t source)
{
    switch (source) {
    case TRACEPARENT_SOURCE_ABSENT:
        return "generated";
    case TRACEPARENT_SOURCE_ACCEPTED:
        return "remote";
    case TRACEPARENT_SOURCE_REJECTED:
        return "rejected";
    default:
        return "unknown";
    }
}
