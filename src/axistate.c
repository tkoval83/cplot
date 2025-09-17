#include "axistate.h"

#include <string.h>
#include <time.h>

static axistate_t g_state;

void axistate_clear (void) { memset (&g_state, 0, sizeof (g_state)); }

void axistate_update (
    const char *phase,
    const char *action,
    int command_rc,
    int wait_rc,
    const ebb_status_snapshot_t *snapshot) {
    axistate_t next;
    memset (&next, 0, sizeof (next));
    next.valid = true;
    next.snapshot_valid = snapshot != NULL;
    next.command_rc = command_rc;
    next.wait_rc = wait_rc;
    if (phase)
        strncpy (next.phase, phase, sizeof (next.phase) - 1);
    if (action)
        strncpy (next.action, action, sizeof (next.action) - 1);
    clock_gettime (CLOCK_REALTIME, &next.ts);
    if (snapshot)
        next.snapshot = *snapshot;
    g_state = next;
}

bool axistate_get (axistate_t *out) {
    if (out)
        *out = g_state;
    return g_state.valid;
}
