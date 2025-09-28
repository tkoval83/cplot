/**
 * @file main.c
 * @brief Точка входу та високорівневий диспетч CLI.
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
 * @brief Основний вхід до застосунку cplot.
 *
 * Розрізняє серверний (`serve`) та клієнтський режими, підключає відповідні
 * обробники та друкує довідкову інформацію за потреби.
 *
 * @param argc Кількість аргументів командного рядка.
 * @param argv Масив аргументів командного рядка.
 * @return 0 при успішному виконанні, інакше 1.
 */
int main (int argc, char *argv[]) {
    if (!setlocale (LC_ALL, ""))
        LOGW ("Не вдалося налаштувати локаль за замовчуванням");
    options_t options;
    options_parser (argc, argv, &options);

    /* Порядок ініціалізації логування:
     * 1) Зчитуємо змінні середовища (CPLOT_LOG, CPLOT_LOG_NO_COLOR) як базову конфігурацію.
     * 2) Потім застосовуємо опції CLI, що мають пріоритет над ENV:
     *      --no-colors      → примусово вимикає кольори, навіть якщо ENV дозволяє.
     *      --verbose        → підвищує рівень до DEBUG поверх CPLOT_LOG.
     *
     * log_init_from_env() викликається після розбору аргументів і до застосування
     * прапорців CLI (verbose/use_colors), щоб змінні середовища були лише базою,
     * а не перезаписували налаштування користувача з CLI.
     */
    log_init_from_env ();

    /* CLI-прапорці переважають: лише явний --no-colors вимикає кольори. */
    if (options.no_colors)
        log_set_use_colors (false);
    /* Поважаємо рівень із ENV; лише --verbose підвищує до DEBUG. */
    if (options.verbose)
        log_set_level (LOG_DEBUG);

    if (options.help) {
        cli_help ();
        return EXIT_SUCCESS;
    }
    if (options.version) {
        cli_print_version ();
        return EXIT_SUCCESS;
    }

    if (options.cmd == CMD_NONE) {
        cli_help ();
        return EXIT_FAILURE;
    }

    int rc = cli_run (&options, argc, argv);
    return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
