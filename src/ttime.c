#include "ttime.h"

double time_diff_ms (const struct timespec *now, const struct timespec *prev) {
    if (!now || !prev)
        return 0.0;
    double diff = (double)(now->tv_sec - prev->tv_sec) * 1000.0;
    diff += (double)(now->tv_nsec - prev->tv_nsec) / 1e6;
    return diff;
}
