/**
 * @file main.c
 * @brief Точка входу та високорівневий диспетч CLI.
 */
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"
#include "help.h"
#include "log.h"
#include "mcp.h"
#include "axistate.h"
#include "trace.h"

/**
 * Точка входу програми.
 *
 * Підтримуються два режими: інтерактивний CLI (без аргументів) та MCP-сервер
 * за прапорцем `--mcp`. Усі інші команди користувач вводить вже в інтерактивній
 * оболонці.
 *
 * @param argc Кількість аргументів командного рядка.
 * @param argv Масив аргументів командного рядка.
 * @return Код завершення процесу.
 */
int main (int argc, char *argv[]) {
    if (!setlocale (LC_ALL, ""))
        LOGW ("Не вдалося налаштувати локаль за замовчуванням");
    axistate_clear ();

    if (trace_enable (NULL) != 0) {
        fprintf (stderr, "[попередження] не вдалося увімкнути трасування у файл\n");
    } else {
        trace_write (LOG_INFO, "запуск програми argc=%d", argc);
    }

    if (argc > 1) {
        const char *arg = argv[1];
        if (strcmp (arg, "--mcp") == 0) {
            trace_write (LOG_INFO, "вмикаємо MCP-режим");
            int mcp_rc = mcp_run ();
            if (mcp_rc == 0)
                trace_write (LOG_INFO, "MCP-режим завершено успішно");
            else
                trace_write (LOG_ERROR, "MCP-режим завершено з кодом %d", mcp_rc);
            trace_disable ();
            return mcp_rc;
        }
        if (strcmp (arg, "--help") == 0 || strcmp (arg, "-h") == 0) {
            trace_write (LOG_INFO, "запитано довідку CLI (режим інтерактивний)");
            help ();
            trace_disable ();
            return EXIT_SUCCESS;
        }
        if (strcmp (arg, "--version") == 0 || strcmp (arg, "-v") == 0) {
            trace_write (LOG_INFO, "запитано версію CLI");
            version ();
            trace_disable ();
            return EXIT_SUCCESS;
        }

        fprintf (stderr, "Невідомий аргумент: %s\n", arg);
        fprintf (stderr, "CLI працює лише в інтерактивному режимі або через прапорець --mcp.\n");
        usage ();
        trace_write (LOG_ERROR, "завершення через невідомий аргумент");
        trace_disable ();
        return EXIT_FAILURE;
    }

    log_set_use_colors (true);
    log_set_level (LOG_INFO);

    trace_write (LOG_INFO, "старт інтерактивного CLI");
    int rc = cli_run_interactive ();
    if (rc != 0) {
        trace_write (LOG_ERROR, "інтерактивний CLI завершився з кодом %d", rc);
        trace_disable ();
        return rc;
    }

    trace_write (LOG_INFO, "інтерактивний CLI завершено успішно");
    trace_disable ();
    return EXIT_SUCCESS;
}
