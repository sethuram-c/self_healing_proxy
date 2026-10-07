#ifndef ROUTING_POLICY_H
#define ROUTING_POLICY_H

#include <stddef.h>
#include <stdint.h>

#include "adaptive_score.h"
#include "latency_signal.h"

#define ROUTING_POLICY_FILE_MAX 4096U
#define ROUTING_POLICY_DEFAULT_ALPHA_NUMERATOR 1U
#define ROUTING_POLICY_DEFAULT_ALPHA_DENOMINATOR 4U
#define ROUTING_POLICY_DEFAULT_STALE_AFTER_MS 5000ULL
#define ROUTING_POLICY_DEFAULT_LATENCY_WEIGHT 1U
#define ROUTING_POLICY_DEFAULT_LOAD_WEIGHT 2U
#define ROUTING_POLICY_DEFAULT_SCORE_SLACK 80U
#define ROUTING_POLICY_MAX_STALE_AFTER_MS 600000ULL
#define ROUTING_POLICY_MAX_SCORE_SLACK ADAPTIVE_SCORE_MAX_LATENCY_RATIO

typedef struct {
    latency_signal_config_t latency;
    adaptive_score_config_t score;
} routing_policy_t;

void routing_policy_defaults(routing_policy_t *policy);
int routing_policy_validate(const routing_policy_t *policy);
int routing_policy_parse(const char *text, routing_policy_t *policy);
int routing_policy_load_file(const char *path, routing_policy_t *policy);
int routing_policy_init_latency_signals(const routing_policy_t *policy,
    latency_signal_t *signals, size_t count);
uint64_t routing_policy_stale_after_ms(const routing_policy_t *policy);

#endif
