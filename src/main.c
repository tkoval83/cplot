/**
 * @file main.c
 * @brief Точка входу до CLI-застосунку cplot.
 * @ingroup main
 *
 * Встановлює локаль, ініціалізує журналювання та делегує виконання
 * підкоманд маршрутизатору CLI.
 */
/**
 * @defgroup main Основний модуль
 * Ініціалізація процесу та делегування виконання підкомандам.
 */
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "args.h"
#include "cli.h"
#include "help.h"
#include "log.h"

/**
 * @brief Запуск застосунку.
 * @param argc Кількість аргументів командного рядка.
 * @param argv Масив аргументів командного рядка.
 * @return Код виходу процесу (0 — успіх, інакше — помилка).
 */
int main (int argc, char *argv[]) {
    if (!setlocale (LC_ALL, ""))
        LOGW ("Не вдалося налаштувати локаль за замовчуванням");
    options_t options;
    args_options_parser (argc, argv, &options);

    log_init_from_env ();

    if (options.no_colors)
        log_set_use_colors (false);

    if (options.verbose)
        log_set_level (LOG_DEBUG);

    if (options.help) {
        help_cli_help ();
        return EXIT_SUCCESS;
    }
    if (options.version) {
        help_cli_print_version ();
        return EXIT_SUCCESS;
    }

    if (options.cmd == CMD_NONE) {
        help_cli_help ();
        return EXIT_FAILURE;
    }

    int rc = cli_run (&options, argc, argv);
    return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
