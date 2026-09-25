#include "remediation_executor.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    process_identity_t identity;
    process_identity_t stale;
    remediation_executor_config_t bad = {0};
    remediation_executor_result_t result;
    char *const argv[] = { "/bin/true", NULL };

    assert(process_identity_capture(getpid(), &identity) == 0);
    assert(identity.pid == getpid());
    assert(identity.start_time_ticks > 0ULL);
    assert(process_identity_matches(&identity) == 1);

    stale = identity;
    stale.start_time_ticks++;
    assert(process_identity_matches(&stale) == 0);

    errno = 0;
    assert(process_identity_capture(-1, &identity) == -1);
    assert(errno == EINVAL);

    errno = 0;
    assert(remediation_executor_restart(&stale, argv, &bad, &result) == -1);
    assert(errno == EINVAL);

    assert(strcmp(remediation_executor_status_name(REMEDIATION_EXECUTOR_RESTARTED), "RESTARTED") == 0);
    assert(strcmp(remediation_executor_status_name(REMEDIATION_EXECUTOR_STALE_IDENTITY), "STALE_IDENTITY") == 0);

    puts("remediation executor tests passed");
    return 0;
}
