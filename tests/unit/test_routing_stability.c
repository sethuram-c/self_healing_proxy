#include "routing_stability.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    routing_stability_t s;
    assert(routing_stability_init(NULL, 3) == -1);
    assert(routing_stability_init(&s, 0) == -1);
    assert(routing_stability_init(&s, ROUTING_STABILITY_MAX_BACKENDS + 1U) == -1);
    assert(routing_stability_init(&s, 3) == 0);

    assert(routing_stability_record_pick(&s, 0) == 0);
    assert(routing_stability_record_pick(&s, 0) == 0);
    assert(routing_stability_record_pick(&s, 1) == 0);
    assert(routing_stability_record_pick(&s, 2) == 0);
    assert(routing_stability_record_pick(&s, 2) == 0);
    assert(routing_stability_record_none(&s) == 0);

    assert(s.decisions == 6U);
    assert(s.no_selection == 1U);
    assert(s.switches == 2U);
    assert(s.max_consecutive == 2U);
    assert(s.picks[0] == 2U);
    assert(s.picks[1] == 1U);
    assert(s.picks[2] == 2U);
    assert(routing_stability_max_picks(&s) == 2U);
    assert(routing_stability_min_picks(&s) == 1U);
    assert(routing_stability_record_pick(&s, 3) == -1);

    puts("routing stability tests passed");
    return 0;
}
