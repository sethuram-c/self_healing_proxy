#ifndef EDGE_LOCALITY_H
#define EDGE_LOCALITY_H

#include <stddef.h>

#define EDGE_LOCALITY_NAME_MAX 32U

typedef struct {
    char region[EDGE_LOCALITY_NAME_MAX];
    char zone[EDGE_LOCALITY_NAME_MAX];
    char site[EDGE_LOCALITY_NAME_MAX];
} edge_locality_t;

typedef enum {
    EDGE_LOCALITY_SITE = 0,
    EDGE_LOCALITY_ZONE = 1,
    EDGE_LOCALITY_REGION = 2,
    EDGE_LOCALITY_REMOTE = 3
} edge_locality_rank_t;

int edge_locality_set(edge_locality_t *locality, const char *region, const char *zone, const char *site);
int edge_locality_validate(const edge_locality_t *locality);
edge_locality_rank_t edge_locality_rank(const edge_locality_t *request, const edge_locality_t *backend);
const char *edge_locality_rank_name(edge_locality_rank_t rank);

#endif
