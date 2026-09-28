#include "healing_event.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static int append_char(char *out, size_t out_size, size_t *used, char c)
{
    if (*used + 1U >= out_size) {
        errno = ENOSPC;
        return -1;
    }
    out[(*used)++] = c;
    out[*used] = '\0';
    return 0;
}

static int append_text(char *out, size_t out_size, size_t *used, const char *text)
{
    size_t len;
    if (text == NULL) text = "";
    len = strlen(text);
    if (len >= out_size - *used) {
        errno = ENOSPC;
        return -1;
    }
    memcpy(out + *used, text, len);
    *used += len;
    out[*used] = '\0';
    return 0;
}

static int append_json_string(char *out, size_t out_size, size_t *used, const char *text)
{
    const unsigned char *p = (const unsigned char *)(text == NULL ? "" : text);
    char esc[7];

    if (append_char(out, out_size, used, '"') != 0) return -1;
    for (; *p != '\0'; p++) {
        switch (*p) {
        case '"': if (append_text(out, out_size, used, "\\\"") != 0) return -1; break;
        case '\\': if (append_text(out, out_size, used, "\\\\") != 0) return -1; break;
        case '\b': if (append_text(out, out_size, used, "\\b") != 0) return -1; break;
        case '\f': if (append_text(out, out_size, used, "\\f") != 0) return -1; break;
        case '\n': if (append_text(out, out_size, used, "\\n") != 0) return -1; break;
        case '\r': if (append_text(out, out_size, used, "\\r") != 0) return -1; break;
        case '\t': if (append_text(out, out_size, used, "\\t") != 0) return -1; break;
        default:
            if (*p < 0x20U) {
                (void)snprintf(esc, sizeof(esc), "\\u%04x", (unsigned int)*p);
                if (append_text(out, out_size, used, esc) != 0) return -1;
            } else if (append_char(out, out_size, used, (char)*p) != 0) {
                return -1;
            }
            break;
        }
    }
    return append_char(out, out_size, used, '"');
}

static int append_key_string(char *out, size_t out_size, size_t *used,
                             const char *key, const char *value, int comma)
{
    if (comma && append_char(out, out_size, used, ',') != 0) return -1;
    if (append_char(out, out_size, used, '"') != 0) return -1;
    if (append_text(out, out_size, used, key) != 0) return -1;
    if (append_text(out, out_size, used, "\":") != 0) return -1;
    return append_json_string(out, out_size, used, value);
}

int healing_event_serialize(const healing_event_t *event, char *out, size_t out_size)
{
    size_t used = 0U;
    char number[64];

    if (event == NULL || out == NULL || out_size == 0U ||
        event->backend == NULL || event->event == NULL ||
        event->health_state == NULL || event->circuit_state == NULL ||
        event->recovery_state == NULL || event->action == NULL ||
        event->reason == NULL) {
        errno = EINVAL;
        return -1;
    }
    out[0] = '\0';
    if (append_char(out, out_size, &used, '{') != 0) return -1;
    if (append_key_string(out, out_size, &used, "schema", "shp.healing.v1", 0) != 0) return -1;
    (void)snprintf(number, sizeof(number), ",\"timestamp_ns\":%llu,\"sequence\":%llu",
                   (unsigned long long)event->timestamp_ns,
                   (unsigned long long)event->sequence);
    if (append_text(out, out_size, &used, number) != 0) return -1;
    if (append_key_string(out, out_size, &used, "backend", event->backend, 1) != 0) return -1;
    if (append_key_string(out, out_size, &used, "event", event->event, 1) != 0) return -1;
    if (append_key_string(out, out_size, &used, "health_state", event->health_state, 1) != 0) return -1;
    if (append_key_string(out, out_size, &used, "circuit_state", event->circuit_state, 1) != 0) return -1;
    if (append_key_string(out, out_size, &used, "recovery_state", event->recovery_state, 1) != 0) return -1;
    if (append_key_string(out, out_size, &used, "action", event->action, 1) != 0) return -1;
    if (append_key_string(out, out_size, &used, "reason", event->reason, 1) != 0) return -1;
    if (append_char(out, out_size, &used, '}') != 0) return -1;
    return 0;
}
