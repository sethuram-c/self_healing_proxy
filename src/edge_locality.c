#include "edge_locality.h"

#include <errno.h>
#include <string.h>

static size_t bounded_len(const char *s, size_t maxlen)
{
    size_t n = 0;
    while (n < maxlen && s[n] != '\0') {
        ++n;
    }
    return n;
}

static int copy_name(char dst[EDGE_LOCALITY_NAME_MAX], const char *src)
{
    if (src == NULL || src[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    size_t n = strlen(src);
    if (n >= EDGE_LOCALITY_NAME_MAX) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(dst, src, n + 1U);
    return 0;
}

int edge_locality_set(edge_locality_t *locality, const char *region, const char *zone, const char *site)
{
    if (locality == NULL) {
        errno = EINVAL;
        return -1;
    }
    edge_locality_t tmp = {0};
    if (copy_name(tmp.region, region) != 0 || copy_name(tmp.zone, zone) != 0 || copy_name(tmp.site, site) != 0) {
        return -1;
    }
    *locality = tmp;
    return 0;
}

int edge_locality_validate(const edge_locality_t *locality)
{
    if (locality == NULL || locality->region[0] == '\0' || locality->zone[0] == '\0' || locality->site[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (bounded_len(locality->region, EDGE_LOCALITY_NAME_MAX) >= EDGE_LOCALITY_NAME_MAX
        || bounded_len(locality->zone, EDGE_LOCALITY_NAME_MAX) >= EDGE_LOCALITY_NAME_MAX
        || bounded_len(locality->site, EDGE_LOCALITY_NAME_MAX) >= EDGE_LOCALITY_NAME_MAX) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

edge_locality_rank_t edge_locality_rank(const edge_locality_t *request, const edge_locality_t *backend)
{
    if (request == NULL || backend == NULL) {
        return EDGE_LOCALITY_REMOTE;
    }
    if (strcmp(request->region, backend->region) != 0) {
        return EDGE_LOCALITY_REMOTE;
    }
    if (strcmp(request->zone, backend->zone) != 0) {
        return EDGE_LOCALITY_REGION;
    }
    if (strcmp(request->site, backend->site) != 0) {
        return EDGE_LOCALITY_ZONE;
    }
    return EDGE_LOCALITY_SITE;
}

const char *edge_locality_rank_name(edge_locality_rank_t rank)
{
    switch (rank) {
    case EDGE_LOCALITY_SITE: return "SITE";
    case EDGE_LOCALITY_ZONE: return "ZONE";
    case EDGE_LOCALITY_REGION: return "REGION";
    case EDGE_LOCALITY_REMOTE: return "REMOTE";
    default: return "INVALID";
    }
}
