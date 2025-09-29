/**
 * @file cli.h
 * @brief Інтерфейс маршрутизатора CLI-підкоманд.
 * @defgroup cli CLI
 * @ingroup main
 * Описує виконання підкоманд та їх параметрів.
 */
#ifndef CPLOT_CLI_H
#define CPLOT_CLI_H

#include "args.h"

/**
 * @brief Виконує вибрану CLI-підкоманду.
 * @ingroup cli
 * @param options Розібрані опції й параметри.
 * @param argc Початковий argc процесу (для делегування підкомандам).
 * @param argv Початковий argv процесу (для делегування підкомандам).
 * @return 0 у разі успіху, інакше код помилки.
 */
int cli_run (const options_t *options, int argc, char *argv[]);

#endif
