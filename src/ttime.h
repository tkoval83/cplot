#ifndef CPLOT_TTIME_H
#define CPLOT_TTIME_H
/**
 * @file ttime.h
 * @brief Допоміжні таймінги/мікросекунди для профілювання.
 * @defgroup ttime Час
 * @ingroup util
 */
#include <time.h>

/**
 * @brief Різниця між часовими мітками у мілісекундах.
 * @param now Поточний час.
 * @param prev Попередній час.
 * @return Різниця у мс (може бути відʼємною).
 */
double time_diff_ms (const struct timespec *now, const struct timespec *prev);

#endif
