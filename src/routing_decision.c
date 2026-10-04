#include "routing_decision.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static int copy_backend(char *dst, size_t cap, const char *src)
{
    if (dst == NULL || cap == 0 || src == NULL || src[0] == '\0') return -1;
    size_t n = 0;
    while (src[n] != '\0' && n < cap) n++;
    if (n == 0 || n >= cap) return -1;
    memcpy(dst, src, n + 1U);
    return 0;
}

const char *routing_fallback_reason_name(routing_fallback_reason_t reason)
{
    switch (reason) {
    case ROUTING_FALLBACK_NONE: return "NONE";
    case ROUTING_FALLBACK_SITE_UNAVAILABLE: return "SITE_UNAVAILABLE";
    case ROUTING_FALLBACK_SITE_ZONE_UNAVAILABLE: return "SITE_ZONE_UNAVAILABLE";
    case ROUTING_FALLBACK_LOCAL_UNAVAILABLE: return "LOCAL_UNAVAILABLE";
    default: return "UNKNOWN";
    }
}

routing_fallback_reason_t routing_fallback_reason_for_rank(edge_locality_rank_t rank)
{
    switch (rank) {
    case EDGE_LOCALITY_SITE: return ROUTING_FALLBACK_NONE;
    case EDGE_LOCALITY_ZONE: return ROUTING_FALLBACK_SITE_UNAVAILABLE;
    case EDGE_LOCALITY_REGION: return ROUTING_FALLBACK_SITE_ZONE_UNAVAILABLE;
    case EDGE_LOCALITY_REMOTE: return ROUTING_FALLBACK_LOCAL_UNAVAILABLE;
    default: return ROUTING_FALLBACK_LOCAL_UNAVAILABLE;
    }
}

int routing_decision_event_init(routing_decision_event_t *event,
    uint64_t timestamp_ns, uint64_t sequence, size_t selected_index,
    const char *backend, edge_locality_rank_t selected_rank,
    uint64_t fastest_fresh_latency_ns, size_t fresh_latency_candidates,
    size_t safe_locality_candidates, uint32_t best_score,
    uint32_t score_threshold, const adaptive_score_result_t *selected_score)
{
    if (event == NULL || selected_score == NULL
        || selected_rank < EDGE_LOCALITY_SITE || selected_rank > EDGE_LOCALITY_REMOTE
        || best_score > score_threshold
        || copy_backend(event->backend, sizeof(event->backend), backend) != 0) {
        errno = EINVAL;
        return -1;
    }
    event->timestamp_ns = timestamp_ns;
    event->sequence = sequence;
    event->selected_index = selected_index;
    event->selected_rank = selected_rank;
    event->fallback_reason = routing_fallback_reason_for_rank(selected_rank);
    event->fastest_fresh_latency_ns = fastest_fresh_latency_ns;
    event->fresh_latency_candidates = fresh_latency_candidates;
    event->safe_locality_candidates = safe_locality_candidates;
    event->best_score = best_score;
    event->score_threshold = score_threshold;
    event->selected_score = *selected_score;
    return 0;
}

static int append_escaped(char *buffer, size_t cap, size_t *used, const char *s)
{
    for (size_t i = 0; s[i] != '\0'; ++i) {
        const char *escape = NULL;
        char unicode[7];
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"': escape = "\\\""; break;
        case '\\': escape = "\\\\"; break;
        case '\b': escape = "\\b"; break;
        case '\f': escape = "\\f"; break;
        case '\n': escape = "\\n"; break;
        case '\r': escape = "\\r"; break;
        case '\t': escape = "\\t"; break;
        default:
            if (c < 0x20U) {
                int n = snprintf(unicode, sizeof(unicode), "\\u%04x", c);
                if (n != 6) return -1;
                escape = unicode;
            }
            break;
        }
        if (escape != NULL) {
            size_t n = strlen(escape);
            if (*used + n >= cap) { errno = ENOSPC; return -1; }
            memcpy(buffer + *used, escape, n);
            *used += n;
        } else {
            if (*used + 1U >= cap) { errno = ENOSPC; return -1; }
            buffer[(*used)++] = (char)c;
        }
    }
    buffer[*used] = '\0';
    return 0;
}

int routing_decision_serialize_json(const routing_decision_event_t *event,
    char *buffer, size_t buffer_size)
{
    if (event == NULL || buffer == NULL || buffer_size == 0) { errno = EINVAL; return -1; }
    size_t used = 0;
    int n = snprintf(buffer, buffer_size,
        "{\"schema\":\"%s\",\"timestamp_ns\":%" PRIu64 ",\"sequence\":%" PRIu64
        ",\"selected_index\":%zu,\"backend\":\"",
        ROUTING_DECISION_SCHEMA, event->timestamp_ns, event->sequence, event->selected_index);
    if (n < 0 || (size_t)n >= buffer_size) { errno = ENOSPC; return -1; }
    used = (size_t)n;
    if (append_escaped(buffer, buffer_size, &used, event->backend) != 0) return -1;
    n = snprintf(buffer + used, buffer_size - used,
        "\",\"locality_rank\":\"%s\",\"fallback_reason\":\"%s\""
        ",\"fastest_fresh_latency_ns\":%" PRIu64
        ",\"fresh_latency_candidates\":%zu,\"safe_locality_candidates\":%zu"
        ",\"best_score\":%u,\"score_threshold\":%u"
        ",\"selected_score\":%u,\"latency_component\":%u,\"load_component\":%u"
        ",\"used_latency\":%s}",
        edge_locality_rank_name(event->selected_rank),
        routing_fallback_reason_name(event->fallback_reason),
        event->fastest_fresh_latency_ns, event->fresh_latency_candidates,
        event->safe_locality_candidates, event->best_score, event->score_threshold,
        event->selected_score.combined_score, event->selected_score.latency_component,
        event->selected_score.load_component,
        event->selected_score.used_latency ? "true" : "false");
    if (n < 0 || (size_t)n >= buffer_size - used) { errno = ENOSPC; return -1; }
    return 0;
}
