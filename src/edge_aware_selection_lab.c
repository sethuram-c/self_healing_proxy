#include "edge_aware_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int run(const char *scenario)
{
    backend_target_t targets[4] = {
        {.name="edge-a",.ip="127.0.0.1",.port=9001,.max_connections=8},
        {.name="edge-b",.ip="127.0.0.1",.port=9002,.max_connections=8},
        {.name="zone-c",.ip="127.0.0.1",.port=9003,.max_connections=8},
        {.name="remote-d",.ip="127.0.0.1",.port=9004,.max_connections=8},
    };
    backend_pool_t pool={.targets=targets,.count=4,.next_index=0};
    backend_health_t health[4]; circuit_breaker_t breakers[4]; edge_locality_t loc[4];
    backend_health_config_t hc={.failure_threshold=3,.recovery_success_threshold=2,.cooldown_ns=10};
    circuit_breaker_config_t cc={.failure_threshold=3,.failure_window_ns=100,.open_duration_ns=50};
    const char *rs[4]={"r1","r1","r1","r2"}, *zs[4]={"z1","z1","z1","z9"}, *ss[4]={"s1","s1","s2","s9"};
    for(size_t i=0;i<4;i++){backend_health_init(&health[i],&hc);backend_health_record_success(&health[i],1);circuit_breaker_init(&breakers[i],&cc);edge_locality_set(&loc[i],rs[i],zs[i],ss[i]);}
    edge_aware_pool_t sel; edge_aware_pool_init(&sel,&pool,health,breakers,loc,4); edge_locality_t req; edge_locality_set(&req,"r1","z1","s1");
    size_t idx; edge_locality_rank_t rank;
    if(strcmp(scenario,"prefer-local")==0){
        for(int n=0;n<4;n++){const backend_target_t *t=edge_aware_pool_acquire_next(&sel,&req,2+n,&idx,&rank); if(!t)return 2; printf("PICK=%s RANK=%s\n",t->name,edge_locality_rank_name(rank)); backend_pool_release(&pool,idx);} }
    else if(strcmp(scenario,"fallback")==0){health[0].state=BACKEND_HEALTH_UNHEALTHY;health[1].state=BACKEND_HEALTH_UNHEALTHY;const backend_target_t*t=edge_aware_pool_acquire_next(&sel,&req,2,&idx,&rank);if(!t)return 2;printf("FIRST=%s RANK=%s\n",t->name,edge_locality_rank_name(rank));backend_pool_release(&pool,idx);health[2].state=BACKEND_HEALTH_UNHEALTHY;t=edge_aware_pool_acquire_next(&sel,&req,3,&idx,&rank);if(!t)return 2;printf("SECOND=%s RANK=%s\n",t->name,edge_locality_rank_name(rank));backend_pool_release(&pool,idx);} 
    else if(strcmp(scenario,"fail-closed")==0){for(int i=0;i<4;i++)health[i].state=BACKEND_HEALTH_UNHEALTHY;const backend_target_t*t=edge_aware_pool_acquire_next(&sel,&req,2,&idx,&rank);printf("SELECTED=%s\n",t?t->name:"NONE");if(t)return 2;}
    else return 2;
    puts("PHASE49_EDGE_AWARE_SELECTION_OK"); return 0;
}

int main(int argc,char **argv){if(argc==2 && strcmp(argv[1],"--help")==0){puts("Usage: edge_aware_selection_lab --scenario prefer-local|fallback|fail-closed");return 0;} if(argc!=3||strcmp(argv[1],"--scenario")!=0)return 2; return run(argv[2]);}
