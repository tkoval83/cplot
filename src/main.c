/**
 * @file main.c
 * @brief Точка входу та високорівневий диспетч CLI.
 */
#include <stdio.h>
#include <stdlib.h>

#include "args.h"
#include "cli.h"
#include "help.h"
#include "log.h"

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

    /* Зчитати опції командного рядка */
    options_t options;
    options_parser (argc, argv, &options);

    /* Налаштувати логер за опціями */
    log_set_use_colors (options.use_colors);
    log_set_level (options.verbose ? LOG_DEBUG : LOG_INFO);

    /* Швидка обробка help/version */
    if (options.help) {
        help ();
        return EXIT_SUCCESS;
    }
    if (options.version) {
        version ();
        return EXIT_SUCCESS;
    }

    /* Виклик підкоманди */
    int rc = cli_dispatch (&options);
    if (rc != 0)
        return rc;

    return EXIT_SUCCESS;
}
