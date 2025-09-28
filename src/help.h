/**
 * @file help.h
 * @brief Довідка для користувацького CLI (клієнтського режиму).
 */
#ifndef CPLOT_CLI_HELP_H
#define CPLOT_CLI_HELP_H

/**
 * @brief Надрукувати розгорнуту довідку для клієнтського режиму.
 */
void cli_help (void);

/**
 * @brief Вивести коротке повідомлення про синтаксис клієнтських команд.
 */
void cli_usage (void);

/**
 * @brief Показати інформацію про версію клієнтського застосунку.
 */
void cli_print_version (void);

#endif /* CPLOT_CLI_HELP_H */
