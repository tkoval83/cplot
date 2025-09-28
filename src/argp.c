/**
 * @file argp.c
 * @brief Реалізація узагальненого парсера аргументів (перенесено з arguments.c).
 *
 * Модуль надає мінімалістичний інтерфейс для реєстрації парі "опція → обробник",
 * підтримує короткі та довгі форми прапорців, автоматичне форматування довідки і
 * виклик користувацьких колбеків для позиційних аргументів.
 */
#include "argp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Перевиділити пам'ять із семантикою "нуль елементів → звільнити".
 *
 * @param ptr       Поточний буфер (може бути NULL).
 * @param new_count Нова кількість елементів.
 * @param elem_size Розмір одного елемента, байт.
 * @return Актуальний буфер або NULL, якщо new_count == 0 чи realloc() не вдалося.
 */
static void *xrealloc (void *ptr, size_t new_count, size_t elem_size) {
    if (new_count == 0) {
        free (ptr);
        return NULL;
    }
    void *p = realloc (ptr, new_count * elem_size);
    return p;
}

/**
 * @brief Забезпечити місткість масиву опцій.
 *
 * Алгоритм подвоює місткість, доки вона не стане не меншою за потрібну, що зменшує кількість
 * дорогих realloc().
 *
 * @param p    Парсер із цільовим масивом.
 * @param need Мінімальна необхідна кількість елементів.
 * @return 0 у разі успіху; -1, якщо не вдалося виділити пам'ять.
 */
static int ensure_capacity (arg_parser_t *p, size_t need) {
    if (p->capacity >= need)
        return 0;
    size_t new_cap = p->capacity ? p->capacity * 2 : 8;
    while (new_cap < need)
        new_cap *= 2;
    void *n = xrealloc (p->options, new_cap, sizeof (arg_option_t));
    if (!n)
        return -1;
    p->options = (arg_option_t *)n;
    p->capacity = new_cap;
    return 0;
}

/**
 * @brief Перевірити збіг аргументу з довгою формою опції.
 *
 * @param opt Опис опції.
 * @param arg Значення з argv, наприклад "--file=...".
 * @return 1, якщо збіг; 0 інакше.
 */
static int matches_long (const arg_option_t *opt, const char *arg) {
    if (!opt->long_name)
        return 0;
    size_t n = strlen (opt->long_name);
    if (strncmp (arg, opt->long_name, n) != 0)
        return 0;
    if (opt->takes_value)
        return arg[n] == '='; /* очікуємо --name=value */
    return arg[n] == '\0';
}

/**
 * @brief Перевірити збіг аргументу з короткою формою опції ("-f").
 *
 * @param opt Опис опції.
 * @param arg Аргумент із argv.
 * @return 1, якщо збіг; 0 інакше.
 */
static int matches_short (const arg_option_t *opt, const char *arg) {
    if (!opt->short_name)
        return 0;
    return strcmp (arg, opt->short_name) == 0;
}

/* ПУБЛІЧНИЙ ІНТЕРФЕЙС */
/**
 * Створити новий парсер аргументів.
 *
 * @param program Ім'я програми для відображення у довідці (може бути NULL; буде оновлено з
 * argv[0]).
 * @return Вказівник на створений парсер або NULL при нестачі пам'яті.
 */
arg_parser_t *arg_parser_create (const char *program) {
    arg_parser_t *p = (arg_parser_t *)calloc (1, sizeof (*p));
    if (!p)
        return NULL;
    p->program = program;
    return p;
}

/**
 * Звільнити усі ресурси, пов'язані з парсером.
 *
 * @param p Парсер (безпечний для передачі NULL).
 */
void arg_parser_destroy (arg_parser_t *p) {
    if (!p)
        return;
    free (p->options);
    free (p);
}

/**
 * Встановити користувацький колбек для друку заголовка довідки.
 *
 * @param p       Парсер (не NULL).
 * @param handler Функція, яку буде викликано для друку заголовка (може бути NULL для вимкнення).
 */
void arg_parser_set_usage (arg_parser_t *p, arg_handler_fn handler) {
    if (p)
        p->usage_handler = handler;
}

/**
 * Встановити обробник за замовчуванням для невідомих/позиційних аргументів.
 *
 * @param p       Парсер (не NULL).
 * @param handler Колбек, який отримує сирий аргумент (може бути NULL для вимкнення).
 */
void arg_parser_set_default (arg_parser_t *p, arg_handler_fn handler) {
    if (p)
        p->default_handler = handler;
}

/**
 * Додати опцію до парсера.
 *
 * @param p           Парсер (не NULL).
 * @param long_name   Довга форма ("--name" без значення), не NULL.
 * @param short_name  Коротка форма ("-n") або NULL.
 * @param takes_value 1 якщо очікується значення у вигляді --name=value.
 * @param handler     Колбек, який буде викликано при збігу (value або NULL).
 * @param usage       Опис для довідки (може бути NULL).
 * @return 0 успіх; -1 при помилці (некоректні аргументи або нестача пам'яті).
 */
int arg_parser_add (
    arg_parser_t *p,
    const char *long_name,
    const char *short_name,
    bool takes_value,
    arg_handler_fn handler,
    const char *usage) {
    if (!p || !long_name || !handler)
        return -1;
    if (ensure_capacity (p, p->count + 1) != 0)
        return -1;
    arg_option_t *opt = &p->options[p->count++];
    opt->long_name = long_name;
    opt->short_name = short_name;
    opt->takes_value = takes_value;
    opt->handler = handler;
    opt->usage = usage;
    return 0;
}

/**
 * Додати опцію за шаблоном: "--name" або "--name=" (означає наявність значення).
 *
 * @param p          Парсер (не NULL).
 * @param pattern    Шаблон довгої форми, наприклад "--file" або "--file=".
 * @param short_name Коротка форма ("-f") або NULL.
 * @param handler    Колбек-обробник збігу.
 * @param usage      Опис для довідки (може бути NULL).
 * @return 0 успіх; -1 при помилці (некоректні аргументи або нестача пам'яті).
 */
int arg_parser_add_auto (
    arg_parser_t *p,
    const char *pattern,
    const char *short_name,
    arg_handler_fn handler,
    const char *usage) {
    if (!pattern)
        return -1;
    const char *eq = strchr (pattern, '=');
    bool takes = eq != NULL;
    char *name_copy = NULL;
    if (takes) {
        size_t len = (size_t)(eq - pattern);
        name_copy = (char *)malloc (len + 1);
        if (!name_copy)
            return -1;
        memcpy (name_copy, pattern, len);
        name_copy[len] = '\0';
        int rc = arg_parser_add (p, name_copy, short_name, true, handler, usage);
        if (rc != 0) {
            free (name_copy);
            return rc;
        }
    } else {
        return arg_parser_add (p, pattern, short_name, false, handler, usage);
    }
    return 0;
}

/**
 * @brief Надрукувати заголовок довідки перед списком опцій.
 *
 * Використовує користувацький колбек, якщо його встановлено, або друкує
 * стандартний рядок "Використання".
 *
 * @param p   Парсер із налаштованим колбеком.
 * @param out Потік для друку (stdout чи stderr).
 */
static void print_usage_header (const arg_parser_t *p, FILE *out) {
    if (p->usage_handler) {
        p->usage_handler (p->program);
    } else if (p->program) {
        fprintf (out, "Використання: %s [опції]\n", p->program ? p->program : "prog");
    }
}

/**
 * Надрукувати таблицю опцій, зареєстрованих у парсері.
 *
 * @param p   Парсер (може бути NULL — тоді нічого не робить).
 * @param out Потік для виводу (не NULL, типово stdout).
 */
void arg_parser_print_options (const arg_parser_t *p, FILE *out) {
    if (!p)
        return;
    size_t max_long = 0, max_short = 0;
    for (size_t i = 0; i < p->count; i++) {
        size_t ll = p->options[i].long_name ? strlen (p->options[i].long_name) : 0;
        size_t sl = p->options[i].short_name ? strlen (p->options[i].short_name) : 0;
        if (ll > max_long)
            max_long = ll;
        if (sl > max_short)
            max_short = sl;
    }
    fprintf (out, "Опції:\n");
    for (size_t i = 0; i < p->count; i++) {
        const arg_option_t *o = &p->options[i];
        fprintf (
            out, "  %-*s  %-*s  %s%s\n", (int)max_long, o->long_name ? o->long_name : "",
            (int)max_short, o->short_name ? o->short_name : "", o->usage ? o->usage : "",
            o->takes_value ? " (значення)" : "");
    }
}

/**
 * @brief Застосувати обробник для знайденої опції, розпарсивши значення.
 *
 * @param p   Парсер, що виконує виклик (залишено на випадок розширень).
 * @param opt Структура з описом опції та обробником.
 * @param arg Початковий аргумент із argv.
 * @return Значення, повернуте обробником (0 успіх; ненульове → помилка).
 */
static int handle_option (arg_parser_t *p, const arg_option_t *opt, const char *arg) {
    const char *value = NULL;
    if (opt->takes_value) {
        const char *eq = strchr (arg, '=');
        if (!eq || !*(eq + 1)) {
            fprintf (
                stderr, "Помилка: опція '%s' потребує значення (формат --name=value).\n",
                opt->long_name);
            return -1;
        }
        value = eq + 1;
    }
    (void)p; /* наразі не використовується */
    return opt->handler (value);
}

/**
 * @brief Розпізнати один аргумент: пошук опції або передача у дефолтний колбек.
 *
 * @param p   Активний парсер.
 * @param arg Поточний аргумент із командного рядка.
 * @return 0 у разі успіху; -1, якщо опція невідома або обробник повернув помилку.
 */
static int parse_one (arg_parser_t *p, const char *arg) {
    for (size_t i = 0; i < p->count; i++) {
        arg_option_t *opt = &p->options[i];
        if (matches_long (opt, arg) || matches_short (opt, arg)) {
            return handle_option (p, opt, arg);
        }
    }
    if (p->default_handler)
        return p->default_handler (arg);
    fprintf (stderr, "Невідомий аргумент: %s\n", arg);
    return -1;
}

/**
 * Розпарсити аргументи командного рядка і викликати обробники.
 *
 * Особливості:
 * - Підтримує --help/-h: друкує заголовок довідки (за наявності) та список опцій і завершує роботу.
 * - Обробляє як довгі, так і короткі форми; значення для опцій передаються як --name=value.
 *
 * @param p    Парсер (не NULL).
 * @param argc Кількість аргументів.
 * @param argv Вектор аргументів.
 * @return 0 успіх; -1 при помилці розбору чи обробки.
 */
int arg_parser_parse (arg_parser_t *p, int argc, const char *argv[]) {
    if (!p || argc < 0)
        return -1;
    p->program = (argc > 0) ? argv[0] : p->program;
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp (arg, "--help") == 0 || strcmp (arg, "-h") == 0) {
            print_usage_header (p, stdout);
            arg_parser_print_options (p, stdout);
            p->printed_usage = 1;
            return 0; /* завершити після help */
        }
        if (parse_one (p, arg) != 0)
            return -1;
    }
    return 0;
}
