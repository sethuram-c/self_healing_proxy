#include "routing_policy.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void test_defaults(void)
{
    routing_policy_t policy;
    routing_policy_defaults(&policy);
    assert(routing_policy_validate(&policy) == 0);
    assert(policy.latency.alpha_numerator == 1U);
    assert(policy.latency.alpha_denominator == 4U);
    assert(routing_policy_stale_after_ms(&policy) == 5000U);
    assert(policy.score.latency_weight == 1U);
    assert(policy.score.load_weight == 2U);
    assert(policy.score.score_slack == 80U);
}

static void test_valid_override_and_latency_init(void)
{
    const char *text =
        "# tuned profile\n"
        "latency_alpha_numerator = 1\n"
        "latency_alpha_denominator = 8\n"
        "latency_stale_after_ms = 7500\n"
        "latency_weight = 10\n"
        "load_weight = 1\n"
        "score_slack = 0\n";
    routing_policy_t policy;
    assert(routing_policy_parse(text, &policy) == 0);
    assert(policy.latency.alpha_denominator == 8U);
    assert(routing_policy_stale_after_ms(&policy) == 7500U);
    assert(policy.score.latency_weight == 10U);
    assert(policy.score.load_weight == 1U);
    assert(policy.score.score_slack == 0U);

    latency_signal_t signals[3];
    assert(routing_policy_init_latency_signals(&policy, signals, 3U) == 0);
    for (size_t i = 0; i < 3U; ++i) {
        assert(signals[i].config.alpha_denominator == 8U);
        assert(signals[i].config.stale_after_ns == 7500000000ULL);
    }
}

static void test_strict_rejection_and_atomicity(void)
{
    routing_policy_t sentinel;
    routing_policy_defaults(&sentinel);
    sentinel.score.score_slack = 123U;
    routing_policy_t policy = sentinel;

    assert(routing_policy_parse("unknown_key=1\n", &policy) == -1);
    assert(memcmp(&policy, &sentinel, sizeof(policy)) == 0);

    assert(routing_policy_parse("latency_weight=1\nlatency_weight=2\n", &policy) == -1);
    assert(memcmp(&policy, &sentinel, sizeof(policy)) == 0);

    assert(routing_policy_parse("latency_weight=-1\n", &policy) == -1);
    assert(routing_policy_parse("latency_weight=abc\n", &policy) == -1);
    assert(routing_policy_parse("latency_alpha_numerator=5\nlatency_alpha_denominator=4\n", &policy) == -1);
    assert(routing_policy_parse("latency_stale_after_ms=600001\n", &policy) == -1);
    assert(routing_policy_parse("latency_weight=0\nload_weight=0\n", &policy) == -1);
    assert(routing_policy_parse("score_slack=4001\n", &policy) == -1);
}

static void test_partial_file_uses_safe_defaults(void)
{
    routing_policy_t policy;
    assert(routing_policy_parse("load_weight=3\n", &policy) == 0);
    assert(policy.score.load_weight == 3U);
    assert(policy.score.latency_weight == 1U);
    assert(policy.score.score_slack == 80U);
    assert(routing_policy_stale_after_ms(&policy) == 5000U);
}

int main(void)
{
    test_defaults();
    test_valid_override_and_latency_init();
    test_strict_rejection_and_atomicity();
    test_partial_file_uses_safe_defaults();
    puts("routing policy configuration tests passed");
    return 0;
}
