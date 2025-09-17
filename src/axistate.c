#include "axistate.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "axidraw.h"

static axistate_t g_state;
static bool g_auto_print = false;

static void axistate_auto_print (const axistate_t *state) {
    if (!g_auto_print || !state)
        return;
    if (!state->valid) {
        fprintf (stdout, "\rстан: недоступний                                ");
        fflush (stdout);
        return;
    }
    const ebb_status_snapshot_t *snap = &state->snapshot;
    int has = state->snapshot_valid ? 1 : 0;
    double pos_x = has ? snap->steps_axis1 / AXIDRAW_STEPS_PER_MM : 0.0;
    double pos_y = has ? snap->steps_axis2 / AXIDRAW_STEPS_PER_MM : 0.0;
    fprintf (
        stdout,
        "\r[%s] %s rc=%d fifo=%s pen=%s pos=(%.1f,%.1f)      ", state->phase[0] ? state->phase : "-",
        state->action[0] ? state->action : "-", state->command_rc,
        (has && snap->motion.fifo_pending) ? "так" : "ні",
        (has && snap->pen_up) ? "вгору" : (has ? "вниз" : "-"), pos_x, pos_y);
    fflush (stdout);
    if (state->phase[0] && strcmp (state->phase, "after") == 0)
        fprintf (stdout, "\n");
    else if (state->phase[0] && strcmp (state->phase, "after_wait") == 0)
        fprintf (stdout, "\n");
}

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
    axistate_auto_print (&g_state);
}

bool axistate_get (axistate_t *out) {
    if (out)
        *out = g_state;
    return g_state.valid;
}

void axistate_enable_auto_print (bool enable) {
    bool prev = g_auto_print;
    g_auto_print = enable;
    if (prev && !enable)
        fprintf (stdout, "\n");
}
