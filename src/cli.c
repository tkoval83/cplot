/**
 * @file cli.c
 * @brief Реалізація інтерактивного CLI.
 */

#include "cli.h"
#include "cmd.h"
#include "log.h"
#include "trace.h"

/**
 * Запустити інтерактивну сесію керування пристроєм.
 *
 * Усі подальші дії користувач виконує командами інтерактивної оболонки. Порт і
 * модель не вказуються на рівні аргументів CLI; їх можна обрати вже всередині
 * оболонки командою `connect` або `model`.
 *
 * @return Код повернення `cmd_device_shell()`.
 */
int cli_run_interactive (void) {
    LOGI ("Запуск інтерактивного режиму");
    trace_write (LOG_INFO, "CLI: інтерактивний режим без попередніх опцій");
    return cmd_device_shell (NULL, NULL, VERBOSE_OFF);
}
