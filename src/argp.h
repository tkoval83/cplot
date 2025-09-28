/**
 * @file argp.h
 * @brief Узагальнений та читабельний модуль розбору аргументів (скорочена назва файлу).
 *
 * Модуль надає об'єктний (через контекст) API без глобальних змінних для
 * реєстрації опцій та їх обробки. Підтримуються довгі та короткі форми, а
 * також автоматичне виведення довідки.
 */
#ifndef CPLOT_ARGP_H
#define CPLOT_ARGP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Тип колбека для опції. */
typedef int (*arg_handler_fn) (const char *value);

/** Дані про одну опцію командного рядка. */
typedef struct arg_option {
    const char *long_name;  /**< Довга форма ("--help"). */
    const char *short_name; /**< Коротка форма ("-h") або NULL. */
    bool takes_value;       /**< Чи потребує значення. */
    arg_handler_fn handler; /**< Колбек, що викликається при збігу. */
    const char *usage;      /**< Рядок із поясненням для довідки. */
} arg_option_t;

/** Контекст парсера. */
typedef struct arg_parser {
    const char *program;            /**< argv[0] або вказане ім'я. */
    arg_option_t *options;          /**< Динамічний масив опцій. */
    size_t count;                   /**< Кількість опцій. */
    size_t capacity;                /**< Виділена місткість. */
    arg_handler_fn usage_handler;   /**< Користувацький usage (може бути NULL). */
    arg_handler_fn default_handler; /**< Обробник невідомих аргументів. */
    int printed_usage;              /**< Внутрішній прапорець. */
} arg_parser_t;

/** Створити новий парсер. @param program Ім'я програми або NULL. @return Вказівник або NULL. */
arg_parser_t *arg_parser_create (const char *program);

/** Звільнити ресурси парсера. @param p Парсер (може бути NULL). */
void arg_parser_destroy (arg_parser_t *p);

/** Встановити вивід заголовку довідки. @param p Парсер. @param handler Колбек. */
void arg_parser_set_usage (arg_parser_t *p, arg_handler_fn handler);

/** Встановити обробник невідомих аргументів. @param p Парсер. @param handler Колбек. */
void arg_parser_set_default (arg_parser_t *p, arg_handler_fn handler);

/** Додати опцію. Див. поля аргументів у описі типу. @return 0 або -1. */
int arg_parser_add (
    arg_parser_t *p,
    const char *long_name,
    const char *short_name,
    bool takes_value,
    arg_handler_fn handler,
    const char *usage);

/** Додати опцію із шаблону ("--name" або "--name="). @return 0 або -1. */
int arg_parser_add_auto (
    arg_parser_t *p,
    const char *pattern,
    const char *short_name,
    arg_handler_fn handler,
    const char *usage);

/** Розпарсити argv. Друкує довідку при --help/-h. @return 0 або -1. */
int arg_parser_parse (arg_parser_t *p, int argc, const char *argv[]);

/** Надрукувати таблицю опцій. @param p Парсер. @param out Потік виводу. */
void arg_parser_print_options (const arg_parser_t *p, FILE *out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CPLOT_ARGP_H */
