#ifndef ADAPTIVE_SCORE_H
#define ADAPTIVE_SCORE_H

#include <stddef.h>
#include <stdint.h>

#define ADAPTIVE_SCORE_SCALE 1000U
#define ADAPTIVE_SCORE_MAX_LATENCY_RATIO 4000U
#define ADAPTIVE_SCORE_MAX_WEIGHT 1000U

typedef struct {
    uint32_t latency_weight;
    uint32_t load_weight;
    uint32_t score_slack;
} adaptive_score_config_t;

typedef struct {
    uint32_t latency_component;
    uint32_t load_component;
    uint32_t combined_score;
    int used_latency;
} adaptive_score_result_t;

int adaptive_score_validate_config(const adaptive_score_config_t *config);
int adaptive_score_compute(uint64_t latency_ns, int has_fresh_latency,
    uint64_t fastest_fresh_latency_ns, size_t active_connections,
    size_t max_connections, const adaptive_score_config_t *config,
    adaptive_score_result_t *result);

#endif
