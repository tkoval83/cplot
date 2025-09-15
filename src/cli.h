/**
 * @file cli.h
 * @brief Інтерфейс диспетчера підкоманд.
 */
#ifndef CLI_H
#define CLI_H

#include "args.h"

/**
 * Викликати відповідний обробник підкоманди.
 *
 * Використовує розібрані опції для запуску обраної підкоманди (print, device,
 * fonts, config, version, sysinfo). Повертає код завершення процесу.
 *
 * @param options Розібрані опції, що визначають підкоманду (не NULL).
 * @return 0 у разі успіху; ненульове значення при помилці.
 */
int cli_dispatch (const options_t *options);

#endif /* CLI_H */
