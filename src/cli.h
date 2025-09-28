/**
 * @file cli.h
 * @brief Обгортка для класичного CLI, що працює через мережевий клієнт.
 */
#ifndef CPLOT_CLI_H
#define CPLOT_CLI_H

#include "args.h"

/**
 * @brief Виконати команду CLI згідно з розібраними опціями.
 *
 * @param options Розібрані параметри командного рядка (не NULL).
 * @param argc    Кількість аргументів у початковому `argv`.
 * @param argv    Оригінальний масив аргументів (не NULL).
 * @return 0 у разі успіху; ненульовий код при помилці виконання команди.
 */
int cli_run (const options_t *options, int argc, char *argv[]);

#endif /* CPLOT_CLI_H */
