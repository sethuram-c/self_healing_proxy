#include "trace_context.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int all_zero(const uint8_t *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        if (p[i] != 0U) {
            return 0;
        }
    }
    return 1;
}

static void test_accepts_valid_traceparent(void)
{
    const char *incoming =
        "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01";
    trace_context_t ctx;
    traceparent_source_t source;
    char trace_hex[TRACE_ID_HEX_LEN + 1U];
    char parent_hex[SPAN_ID_HEX_LEN + 1U];
    char formatted[TRACEPARENT_TEXT_LEN + 1U];

    assert(trace_context_start_request(&ctx, incoming, &source) == 0);
    assert(source == TRACEPARENT_SOURCE_ACCEPTED);
    assert(ctx.has_remote_parent == 1);
    assert(ctx.trace_flags == 0x01U);
    assert(trace_context_trace_id_hex(&ctx, trace_hex, sizeof(trace_hex)) == 0);
    assert(strcmp(trace_hex, "4bf92f3577b34da6a3ce929d0e0e4736") == 0);
    assert(trace_context_parent_span_id_hex(&ctx, parent_hex, sizeof(parent_hex)) == 0);
    assert(strcmp(parent_hex, "00f067aa0ba902b7") == 0);
    assert(trace_context_format_traceparent(&ctx, formatted, sizeof(formatted)) == 0);
    assert(strncmp(formatted, "00-4bf92f3577b34da6a3ce929d0e0e4736-", 36U) == 0);
    assert(strcmp(formatted + 53, "01") == 0);
}

static void test_invalid_traceparents_fall_back_to_generated(void)
{
    const char *cases[] = {
        "00-00000000000000000000000000000000-00f067aa0ba902b7-01",
        "00-4bf92f3577b34da6a3ce929d0e0e4736-0000000000000000-01",
        "00-4BF92F3577B34DA6A3CE929D0E0E4736-00f067aa0ba902b7-01",
        "01-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01",
        "00_4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01",
        "short"
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        trace_context_t ctx;
        traceparent_source_t source;
        assert(trace_context_start_request(&ctx, cases[i], &source) == 0);
        assert(source == TRACEPARENT_SOURCE_REJECTED);
        assert(ctx.has_remote_parent == 0);
        assert(!all_zero(ctx.trace_id, TRACE_ID_BYTES));
        assert(!all_zero(ctx.request_id, REQUEST_ID_BYTES));
        assert(!all_zero(ctx.span_id, SPAN_ID_BYTES));
    }
}

static void test_absent_generates_nonzero_ids(void)
{
    trace_context_t a;
    trace_context_t b;
    traceparent_source_t sa;
    traceparent_source_t sb;

    assert(trace_context_start_request(&a, NULL, &sa) == 0);
    assert(trace_context_start_request(&b, "", &sb) == 0);
    assert(sa == TRACEPARENT_SOURCE_ABSENT);
    assert(sb == TRACEPARENT_SOURCE_ABSENT);
    assert(!all_zero(a.trace_id, TRACE_ID_BYTES));
    assert(!all_zero(a.request_id, REQUEST_ID_BYTES));
    assert(!all_zero(a.span_id, SPAN_ID_BYTES));
    assert(memcmp(a.trace_id, b.trace_id, TRACE_ID_BYTES) != 0);
    assert(memcmp(a.request_id, b.request_id, REQUEST_ID_BYTES) != 0);
}

static void test_retry_span_changes_but_request_and_trace_stay_stable(void)
{
    trace_context_t ctx;
    traceparent_source_t source;
    uint8_t trace_before[TRACE_ID_BYTES];
    uint8_t request_before[REQUEST_ID_BYTES];
    uint8_t span_before[SPAN_ID_BYTES];

    assert(trace_context_start_request(&ctx, NULL, &source) == 0);
    memcpy(trace_before, ctx.trace_id, sizeof(trace_before));
    memcpy(request_before, ctx.request_id, sizeof(request_before));
    memcpy(span_before, ctx.span_id, sizeof(span_before));

    assert(trace_context_next_attempt_span(&ctx) == 0);
    assert(memcmp(trace_before, ctx.trace_id, sizeof(trace_before)) == 0);
    assert(memcmp(request_before, ctx.request_id, sizeof(request_before)) == 0);
    assert(memcmp(span_before, ctx.span_id, sizeof(span_before)) != 0);
}

int main(void)
{
    test_accepts_valid_traceparent();
    test_invalid_traceparents_fall_back_to_generated();
    test_absent_generates_nonzero_ids();
    test_retry_span_changes_but_request_and_trace_stay_stable();
    puts("trace context tests passed");
    return 0;
}
