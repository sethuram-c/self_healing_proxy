#include "routing_policy.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NS_PER_MS 1000000ULL

enum {
    KEY_ALPHA_NUM = 1U << 0,
    KEY_ALPHA_DEN = 1U << 1,
    KEY_STALE_MS = 1U << 2,
    KEY_LATENCY_WEIGHT = 1U << 3,
    KEY_LOAD_WEIGHT = 1U << 4,
    KEY_SCORE_SLACK = 1U << 5
};

static char *trim(char *s)
{
    while (*s != '\0' && isspace((unsigned char)*s)) {
        ++s;
    }
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        --end;
    }
    *end = '\0';
    return s;
}

static int parse_u64(const char *text, uint64_t max_value, uint64_t *value)
{
    if (text == NULL || value == NULL || *text == '\0') {
        return -1;
    }
    for (const char *p = text; *p != '\0'; ++p) {
        if (!isdigit((unsigned char)*p)) {
            return -1;
        }
    }
    errno = 0;
    char *end = NULL;
    unsigned long long parsed = strtoull(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || parsed > max_value) {
        return -1;
    }
    *value = (uint64_t)parsed;
    return 0;
}

void routing_policy_defaults(routing_policy_t *policy)
{
    if (policy == NULL) {
        return;
    }
    policy->latency.alpha_numerator = ROUTING_POLICY_DEFAULT_ALPHA_NUMERATOR;
    policy->latency.alpha_denominator = ROUTING_POLICY_DEFAULT_ALPHA_DENOMINATOR;
    policy->latency.stale_after_ns = ROUTING_POLICY_DEFAULT_STALE_AFTER_MS * NS_PER_MS;
    policy->score.latency_weight = ROUTING_POLICY_DEFAULT_LATENCY_WEIGHT;
    policy->score.load_weight = ROUTING_POLICY_DEFAULT_LOAD_WEIGHT;
    policy->score.score_slack = ROUTING_POLICY_DEFAULT_SCORE_SLACK;
}

int routing_policy_validate(const routing_policy_t *policy)
{
    if (policy == NULL || policy->latency.alpha_numerator == 0
        || policy->latency.alpha_denominator == 0
        || policy->latency.alpha_numerator > policy->latency.alpha_denominator
        || policy->latency.alpha_denominator > LATENCY_SIGNAL_ALPHA_DEN_MAX
        || policy->latency.stale_after_ns == 0
        || policy->latency.stale_after_ns > ROUTING_POLICY_MAX_STALE_AFTER_MS * NS_PER_MS
        || adaptive_score_validate_config(&policy->score) != 0
        || policy->score.score_slack > ROUTING_POLICY_MAX_SCORE_SLACK) {
        return -1;
    }
    return 0;
}

static int set_key(routing_policy_t *candidate, const char *key, const char *value,
    unsigned int *seen)
{
    uint64_t parsed = 0;
    unsigned int bit = 0;

    if (strcmp(key, "latency_alpha_numerator") == 0) {
        bit = KEY_ALPHA_NUM;
        if (parse_u64(value, UINT32_MAX, &parsed) != 0) return -1;
        candidate->latency.alpha_numerator = (uint32_t)parsed;
    } else if (strcmp(key, "latency_alpha_denominator") == 0) {
        bit = KEY_ALPHA_DEN;
        if (parse_u64(value, UINT32_MAX, &parsed) != 0) return -1;
        candidate->latency.alpha_denominator = (uint32_t)parsed;
    } else if (strcmp(key, "latency_stale_after_ms") == 0) {
        bit = KEY_STALE_MS;
        if (parse_u64(value, ROUTING_POLICY_MAX_STALE_AFTER_MS, &parsed) != 0) return -1;
        candidate->latency.stale_after_ns = parsed * NS_PER_MS;
    } else if (strcmp(key, "latency_weight") == 0) {
        bit = KEY_LATENCY_WEIGHT;
        if (parse_u64(value, ADAPTIVE_SCORE_MAX_WEIGHT, &parsed) != 0) return -1;
        candidate->score.latency_weight = (uint32_t)parsed;
    } else if (strcmp(key, "load_weight") == 0) {
        bit = KEY_LOAD_WEIGHT;
        if (parse_u64(value, ADAPTIVE_SCORE_MAX_WEIGHT, &parsed) != 0) return -1;
        candidate->score.load_weight = (uint32_t)parsed;
    } else if (strcmp(key, "score_slack") == 0) {
        bit = KEY_SCORE_SLACK;
        if (parse_u64(value, ROUTING_POLICY_MAX_SCORE_SLACK, &parsed) != 0) return -1;
        candidate->score.score_slack = (uint32_t)parsed;
    } else {
        return -1;
    }

    if ((*seen & bit) != 0) {
        return -1;
    }
    *seen |= bit;
    return 0;
}

int routing_policy_parse(const char *text, routing_policy_t *policy)
{
    if (text == NULL || policy == NULL || strlen(text) > ROUTING_POLICY_FILE_MAX) {
        return -1;
    }

    char buffer[ROUTING_POLICY_FILE_MAX + 1U];
    size_t len = strlen(text);
    memcpy(buffer, text, len + 1U);

    routing_policy_t candidate;
    routing_policy_defaults(&candidate);
    unsigned int seen = 0;

    char *cursor = buffer;
    while (*cursor != '\0') {
        char *line = cursor;
        char *newline = strchr(cursor, '\n');
        if (newline != NULL) {
            *newline = '\0';
            cursor = newline + 1;
        } else {
            cursor += strlen(cursor);
        }

        line = trim(line);
        if (*line == '\0' || *line == '#') {
            continue;
        }

        char *equals = strchr(line, '=');
        if (equals == NULL || strchr(equals + 1, '=') != NULL) {
            return -1;
        }
        *equals = '\0';
        char *key = trim(line);
        char *value = trim(equals + 1);
        if (*key == '\0' || *value == '\0' || set_key(&candidate, key, value, &seen) != 0) {
            return -1;
        }
    }

    if (routing_policy_validate(&candidate) != 0) {
        return -1;
    }
    *policy = candidate;
    return 0;
}

int routing_policy_load_file(const char *path, routing_policy_t *policy)
{
    if (path == NULL || policy == NULL) {
        return -1;
    }
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return -1;
    }

    char buffer[ROUTING_POLICY_FILE_MAX + 1U];
    size_t used = fread(buffer, 1, ROUTING_POLICY_FILE_MAX + 1U, fp);
    int read_error = ferror(fp);
    int close_error = fclose(fp);
    if (read_error || close_error != 0 || used > ROUTING_POLICY_FILE_MAX) {
        return -1;
    }
    buffer[used] = '\0';
    return routing_policy_parse(buffer, policy);
}

int routing_policy_init_latency_signals(const routing_policy_t *policy,
    latency_signal_t *signals, size_t count)
{
    if (routing_policy_validate(policy) != 0 || signals == NULL || count == 0) {
        return -1;
    }
    for (size_t i = 0; i < count; ++i) {
        if (latency_signal_init(&signals[i], &policy->latency) != 0) {
            return -1;
        }
    }
    return 0;
}

uint64_t routing_policy_stale_after_ms(const routing_policy_t *policy)
{
    if (policy == NULL) {
        return 0;
    }
    return policy->latency.stale_after_ns / NS_PER_MS;
}
