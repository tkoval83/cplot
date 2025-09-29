/**
 * @file help.h
 * @brief Генерація короткої довідки CLI з описів у args.c.
 * @defgroup help Хелп
 * @ingroup cli
 * @details
 * Модуль формує розділи "Використання", "Команди" та групи опцій на основі
 * описів, що надаються парсером аргументів (`args.c`). Вся інформація друкується
 * українською мовою у `stdout`.
 */
#ifndef CPLOT_CLI_HELP_H
#define CPLOT_CLI_HELP_H

/**
 * @brief Виводить повну довідку CLI у `stdout`.
 * @details Містить використання, загальний опис, глобальні опції, опції команд,
 * дії пристрою, ключі конфігурації та автора. Джерело правди — визначення в
 * `args.c`/`args.h` і константи з `proginfo.h`.
 */
void cli_help (void);

/**
 * @brief Виводить короткий розділ "Використання" у `stdout`.
 */
void cli_usage (void);

/**
 * @brief Виводить рядок версії застосунку у `stdout`.
 * @details Формат: "<name> версія <version>".
 */
void cli_print_version (void);

#endif
