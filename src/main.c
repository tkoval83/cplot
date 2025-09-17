/**
 * @file main.c
 * @brief Точка входу та високорівневий диспетч CLI.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "cli.h"
#include "help.h"
#include "log.h"
#include "mcp.h"
#include "axistate.h"
#include "trace.h"

/**
 * Точка входу програми.
 *
 * Парсить аргументи командного рядка, обробляє швидкі прапорці help/version і
 * передає керування диспетчеру підкоманд.
 *
 * @param argc Кількість аргументів командного рядка.
 * @param argv Масив аргументів командного рядка.
 * @return Код завершення процесу: 0 при успіху або код помилки підкоманди.
 */
int main (int argc, char *argv[]) {
    axistate_clear ();

    if (trace_enable (NULL) != 0) {
        fprintf (stderr, "[попередження] не вдалося увімкнути трасування у файл\n");
    } else {
        trace_write (LOG_INFO, "запуск програми argc=%d", argc);
    }
    /* Спеціальний режим: якщо перший аргумент дорівнює "--mcp", запускаємо MCP‑сервер. */
    if (argc > 1 && argv[1] && strcmp (argv[1], "--mcp") == 0) {
        trace_write (LOG_INFO, "вмикаємо MCP-режим");
        int mcp_rc = mcp_run ();
        if (mcp_rc == 0)
            trace_write (LOG_INFO, "MCP-режим завершено успішно");
        else
            trace_write (LOG_ERROR, "MCP-режим завершено з кодом %d", mcp_rc);
        trace_disable ();
        return mcp_rc;
    }

    /* Зчитати опції командного рядка */
    options_t options;
    options_parser (argc, argv, &options);

    /* Налаштувати логер за опціями */
    log_set_use_colors (options.use_colors);
    log_set_level (options.verbose ? LOG_DEBUG : LOG_INFO);

    /* Швидка обробка help/version */
    if (options.help) {
        trace_write (LOG_INFO, "запитано довідку CLI");
        help ();
        trace_write (LOG_INFO, "вихід після показу довідки");
        trace_disable ();
        return EXIT_SUCCESS;
    }
    if (options.version) {
        trace_write (LOG_INFO, "запитано версію CLI");
        version ();
        trace_write (LOG_INFO, "вихід після показу версії");
        trace_disable ();
        return EXIT_SUCCESS;
    }

    /* Виклик підкоманди */
    int rc = cli_dispatch (&options);
    if (rc != 0) {
        trace_write (LOG_ERROR, "програма завершена з кодом %d", rc);
        trace_disable ();
        return rc;
    }

    trace_write (LOG_INFO, "програма завершена успішно");
    trace_disable ();
    return EXIT_SUCCESS;
}
