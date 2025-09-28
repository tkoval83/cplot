#ifndef CPLOT_TTIME_H
#define CPLOT_TTIME_H

#include <time.h>

/** Compute the millisecond difference between two timestamps. */
double time_diff_ms (const struct timespec *now, const struct timespec *prev);

#endif /* CPLOT_TTIME_H */
