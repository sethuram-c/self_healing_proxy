#include "json_log.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static trace_context_t make_context(void)
{
    trace_context_t ctx;
    traceparent_source_t source;
    int rc = trace_context_start_request(
        &ctx,
        "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01",
        &source);
    assert(rc == 0);
    assert(source == TRACEPARENT_SOURCE_ACCEPTED);
    return ctx;
}

static void test_escape(void)
{
    char out[256];
    size_t written = 0U;
    int rc = json_log_escape_string("quote=\" slash=\\ newline=\n tab=\t ctrl=\x01", out, sizeof(out), &written);
    assert(rc == 0);
    assert(written == strlen(out));
    assert(strcmp(out, "\"quote=\\\" slash=\\\\ newline=\\n tab=\\t ctrl=\\u0001\"") == 0);
}

static void test_schema_and_identity(void)
{
    trace_context_t ctx = make_context();
    json_log_event_t event;
    char out[JSON_LOG_MAX_LINE];
    int n;

    event.timestamp_unix_ns = 123456789ULL;
    event.event = "backend_attempt";
    event.backend = "backend-a";
    event.result = "retry";
    event.message = "upstream reset";
    event.attempt = 2U;
    event.trace_source = TRACEPARENT_SOURCE_ACCEPTED;
    event.trace = &ctx;

    n = json_log_serialize(&event, out, sizeof(out));
    assert(n > 0);
    assert((size_t)n == strlen(out));
    assert(strstr(out, "\"schema\":\"shp.telemetry.v1\"") != NULL);
    assert(strstr(out, "\"timestamp_unix_ns\":123456789") != NULL);
    assert(strstr(out, "\"event\":\"backend_attempt\"") != NULL);
    assert(strstr(out, "\"trace_id\":\"4bf92f3577b34da6a3ce929d0e0e4736\"") != NULL);
    assert(strstr(out, "\"attempt\":2") != NULL);
    assert(strchr(out, '\n') == NULL);
}

static void test_bounds(void)
{
    trace_context_t ctx = make_context();
    json_log_event_t event;
    char tiny[16];
    char too_long[JSON_LOG_MAX_MESSAGE + 2U];

    memset(too_long, 'x', sizeof(too_long));
    too_long[sizeof(too_long) - 1U] = '\0';

    event.timestamp_unix_ns = 1U;
    event.event = "event";
    event.backend = "backend";
    event.result = "ok";
    event.message = "hello";
    event.attempt = 1U;
    event.trace_source = TRACEPARENT_SOURCE_ACCEPTED;
    event.trace = &ctx;

    errno = 0;
    assert(json_log_serialize(&event, tiny, sizeof(tiny)) < 0);
    assert(errno == ENOSPC);

    event.message = too_long;
    errno = 0;
    assert(json_log_serialize(&event, (char[JSON_LOG_MAX_LINE]){0}, JSON_LOG_MAX_LINE) < 0);
    assert(errno == E2BIG);
}

static void test_invalid_input(void)
{
    char out[64];
    errno = 0;
    assert(json_log_serialize(NULL, out, sizeof(out)) < 0);
    assert(errno == EINVAL);
}

int main(void)
{
    test_escape();
    test_schema_and_identity();
    test_bounds();
    test_invalid_input();
    puts("structured JSON log tests passed");
    return 0;
}
