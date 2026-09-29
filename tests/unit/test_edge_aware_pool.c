#include "edge_aware_pool.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void setup(backend_pool_t *pool, backend_target_t targets[4], backend_health_t health[4],
    circuit_breaker_t breakers[4], edge_locality_t loc[4])
{
    backend_target_t seed[4] = {
        {.name="a", .ip="127.0.0.1", .port=1, .max_connections=8},
        {.name="b", .ip="127.0.0.1", .port=2, .max_connections=8},
        {.name="c", .ip="127.0.0.1", .port=3, .max_connections=8},
        {.name="d", .ip="127.0.0.1", .port=4, .max_connections=8},
    };
    memcpy(targets, seed, sizeof(seed));
    pool->targets = targets; pool->count = 4; pool->next_index = 0;
    backend_health_config_t hc = {.failure_threshold=3,.recovery_success_threshold=2,.cooldown_ns=10};
    circuit_breaker_config_t cc = {.failure_threshold=3,.failure_window_ns=100,.open_duration_ns=50};
    for (size_t i=0;i<4;i++) { assert(backend_health_init(&health[i], &hc)==0); assert(backend_health_record_success(&health[i],1)==0); assert(circuit_breaker_init(&breakers[i],&cc)==0); }
    assert(edge_locality_set(&loc[0],"r1","z1","s1")==0);
    assert(edge_locality_set(&loc[1],"r1","z1","s1")==0);
    assert(edge_locality_set(&loc[2],"r1","z1","s2")==0);
    assert(edge_locality_set(&loc[3],"r2","z9","s9")==0);
}

int main(void)
{
    backend_pool_t pool={0}; backend_target_t t[4]; backend_health_t h[4]; circuit_breaker_t b[4]; edge_locality_t l[4];
    setup(&pool,t,h,b,l); edge_aware_pool_t s; assert(edge_aware_pool_init(&s,&pool,h,b,l,4)==0);
    edge_locality_t req; assert(edge_locality_set(&req,"r1","z1","s1")==0);
    size_t idx; edge_locality_rank_t rank;
    assert(edge_aware_pool_acquire_next(&s,&req,2,&idx,&rank)!=NULL); assert(idx==0 && rank==EDGE_LOCALITY_SITE); backend_pool_release(&pool,idx);
    assert(edge_aware_pool_acquire_next(&s,&req,3,&idx,&rank)!=NULL); assert(idx==1 && rank==EDGE_LOCALITY_SITE); backend_pool_release(&pool,idx);
    h[0].state=BACKEND_HEALTH_UNHEALTHY; h[1].state=BACKEND_HEALTH_UNHEALTHY;
    assert(edge_aware_pool_acquire_next(&s,&req,4,&idx,&rank)!=NULL); assert(idx==2 && rank==EDGE_LOCALITY_ZONE); backend_pool_release(&pool,idx);
    h[2].state=BACKEND_HEALTH_UNHEALTHY;
    assert(edge_aware_pool_acquire_next(&s,&req,5,&idx,&rank)!=NULL); assert(idx==3 && rank==EDGE_LOCALITY_REMOTE); backend_pool_release(&pool,idx);
    h[3].state=BACKEND_HEALTH_UNHEALTHY;
    size_t cursor=pool.next_index; assert(edge_aware_pool_acquire_next(&s,&req,6,&idx,&rank)==NULL); assert(pool.next_index==cursor);
    printf("edge-aware backend selection tests passed\n");
    return 0;
}
