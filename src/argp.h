/**
 * @file argp.h
 * @brief Проста допоміжна бібліотека для побудови парсера аргументів.
 * @defgroup argp ArgParser
 * @ingroup cli
 */
#ifndef CPLOT_ARGP_H
#define CPLOT_ARGP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Колбек-обробник значення опції. */
typedef int (*arg_handler_fn) (const char *value);

/**
 * @brief Опис однієї опції для парсера.
 */
typedef struct arg_option {
    const char *long_name;
    const char *short_name;
    bool takes_value;
    arg_handler_fn handler;
    const char *usage;
} arg_option_t;

/**
 * @brief Конфігурація та стан парсера аргументів.
 */
typedef struct arg_parser {
    const char *program;
    arg_option_t *options;
    size_t count;
    size_t capacity;
    arg_handler_fn usage_handler;
    arg_handler_fn default_handler;
    int printed_usage;
} arg_parser_t;

/**
 * @brief Створює новий парсер.
 * @param program Імʼя програми (для usage).
 * @return Вказівник на парсер або NULL.
 */
arg_parser_t *arg_parser_create (const char *program);

/**
 * @brief Знищує парсер і вивільняє ресурси.
 * @param p Парсер (може бути NULL).
 */
void arg_parser_destroy (arg_parser_t *p);

/**
 * @brief Встановлює обробник хелпу/використання.
 * @param p Парсер.
 * @param handler Функція, що друкує usage (отримує program).
 */
void arg_parser_set_usage (arg_parser_t *p, arg_handler_fn handler);

/**
 * @brief Встановлює обробник для позиційних аргументів.
 * @param p Парсер.
 * @param handler Обробник, що отримує значення аргументу.
 */
void arg_parser_set_default (arg_parser_t *p, arg_handler_fn handler);

/**
 * @brief Додає опцію до парсера.
 * @param p Парсер.
 * @param long_name Довге імʼя без "--".
 * @param short_name Коротке імʼя з "-" або NULL.
 * @param takes_value Чи потребує значення (--name=value).
 * @param handler Обробник значення.
 * @param usage Опис для довідки.
 * @return 0 — успіх, -1 — помилка.
 */
int arg_parser_add (
    arg_parser_t *p,
    const char *long_name,
    const char *short_name,
    bool takes_value,
    arg_handler_fn handler,
    const char *usage);

/**
 * @brief Додає опції за шаблоном `name` або `name=VALUE`.
 * @param p Парсер.
 * @param pattern Шаблон імені (без "--").
 * @param short_name Коротке імʼя або NULL.
 * @param handler Обробник значення.
 * @param usage Опис для довідки.
 * @return 0 — успіх, -1 — помилка.
 */
int arg_parser_add_auto (
    arg_parser_t *p,
    const char *pattern,
    const char *short_name,
    arg_handler_fn handler,
    const char *usage);

/**
 * @brief Запускає розбір аргументів.
 * @param p Парсер.
 * @param argc Кількість аргументів.
 * @param argv Масив аргументів.
 * @return 0 — успіх (або після друку --help), -1 — помилка.
 */
int arg_parser_parse (arg_parser_t *p, int argc, const char *argv[]);

/**
 * @brief Друкує перелік опцій у потік.
 * @param p Парсер.
 * @param out Потік виводу.
 */
void arg_parser_print_options (const arg_parser_t *p, FILE *out);

#ifdef __cplusplus
}
#endif

#endif
