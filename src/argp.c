/**
 * @file argp.c
 * @brief Реалізація простого парсера аргументів.
 * @ingroup argp
 */
#include "argp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Безпечний realloc для масивів (count * elem_size).
 * @param ptr Поточний вказівник (або NULL).
 * @param new_count Нова кількість елементів.
 * @param elem_size Розмір одного елемента.
 * @return Новий вказівник або NULL (при new_count=0 або помилці).
 */
static void *argp_xrealloc (void *ptr, size_t new_count, size_t elem_size) {
    if (new_count == 0) {
        free (ptr);
        return NULL;
    }
    void *p = realloc (ptr, new_count * elem_size);
    return p;
}

/**
 * @brief Гарантує мінімальну ємність масиву опцій у парсері.
 * @param p Парсер.
 * @param need Потрібна кількість елементів.
 * @return 0 — ємність достатня/збільшено, -1 — помилка виділення.
 */
static int argp_ensure_capacity (arg_parser_t *p, size_t need) {
    if (p->capacity >= need)
        return 0;
    size_t new_cap = p->capacity ? p->capacity * 2 : 8;
    while (new_cap < need)
        new_cap *= 2;
    void *n = argp_xrealloc (p->options, new_cap, sizeof (arg_option_t));
    if (!n)
        return -1;
    p->options = (arg_option_t *)n;
    p->capacity = new_cap;
    return 0;
}

/**
 * @brief Перевіряє відповідність довгого аргументу виду --name[=value].
 * @param opt Опис опції.
 * @param arg Аргумент командного рядка без початкових тире.
 * @return 1 — співпадає, 0 — ні.
 */
static int argp_matches_long (const arg_option_t *opt, const char *arg) {
    if (!opt->long_name)
        return 0;
    size_t n = strlen (opt->long_name);
    if (strncmp (arg, opt->long_name, n) != 0)
        return 0;
    if (opt->takes_value)
        return arg[n] == '=';
    return arg[n] == '\0';
}

/**
 * @brief Перевіряє відповідність короткого аргументу на кшталт -h.
 * @param opt Опис опції.
 * @param arg Аргумент (включно з префіксом '-').
 * @return 1 — співпадає, 0 — ні.
 */
static int argp_matches_short (const arg_option_t *opt, const char *arg) {
    if (!opt->short_name)
        return 0;
    return strcmp (arg, opt->short_name) == 0;
}

/**
 * @brief Створює новий парсер аргументів.
 * @param program Імʼя програми (для хелпу).
 * @return Вказівник на парсер або NULL.
 */
arg_parser_t *argp_arg_parser_create (const char *program) {
    arg_parser_t *p = (arg_parser_t *)calloc (1, sizeof (*p));
    if (!p)
        return NULL;
    p->program = program;
    return p;
}

/**
 * @brief Звільняє ресурси парсера і його таблиці опцій.
 * @param p Парсер (може бути NULL).
 */
void argp_arg_parser_destroy (arg_parser_t *p) {
    if (!p)
        return;
    free (p->options);
    free (p);
}

/**
 * @brief Встановлює користувацький обробник виводу usage.
 * @param p Парсер.
 * @param handler Колбек для друку usage (отримує program).
 */
void argp_arg_parser_set_usage (arg_parser_t *p, arg_handler_fn handler) {
    if (p)
        p->usage_handler = handler;
}

/**
 * @brief Встановлює обробник позиційних/невідомих аргументів.
 * @param p Парсер.
 * @param handler Колбек, що отримує значення.
 */
void argp_arg_parser_set_default (arg_parser_t *p, arg_handler_fn handler) {
    if (p)
        p->default_handler = handler;
}

/**
 * @brief Додає опцію у парсер.
 * @param p Парсер.
 * @param long_name Довге імʼя без префіксу (--).
 * @param short_name Коротке імʼя з префіксом (-h) або NULL.
 * @param takes_value Чи потребує значення (--opt=value).
 * @param handler Колбек-обробник значення.
 * @param usage Опис для хелпу.
 * @return 0 — успіх, -1 — помилка.
 */
int argp_arg_parser_add (
    arg_parser_t *p,
    const char *long_name,
    const char *short_name,
    bool takes_value,
    arg_handler_fn handler,
    const char *usage) {
    if (!p || !long_name || !handler)
        return -1;
    if (argp_ensure_capacity (p, p->count + 1) != 0)
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
 * @brief Додає опцію за шаблоном "name" або "name=value".
 * @param p Парсер.
 * @param pattern Шаблон (без префіксу --).
 * @param short_name Коротке імʼя або NULL.
 * @param handler Обробник значення.
 * @param usage Опис для хелпу.
 * @return 0 — успіх, -1 — помилка.
 */
int argp_arg_parser_add_auto (
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
        int rc = argp_arg_parser_add (p, name_copy, short_name, true, handler, usage);
        if (rc != 0) {
            free (name_copy);
            return rc;
        }
    } else {
        return argp_arg_parser_add (p, pattern, short_name, false, handler, usage);
    }
    return 0;
}

/**
 * @brief Друкує заголовок usage або викликає користувацький колбек.
 * @param p Парсер.
 * @param out Потік виводу.
 */
static void argp_print_usage_header (const arg_parser_t *p, FILE *out) {
    if (p->usage_handler) {
        p->usage_handler (p->program);
    } else if (p->program) {
        fprintf (out, "Використання: %s [опції]\n", p->program ? p->program : "prog");
    }
}

/**
 * @brief Друкує таблицю опцій із колонками long/short/usage.
 * @param p Парсер.
 * @param out Потік виводу.
 */
void argp_arg_parser_print_options (const arg_parser_t *p, FILE *out) {
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
 * @brief Викликає обробник конкретної опції, видобуваючи значення.
 * @param p Парсер.
 * @param opt Опис опції.
 * @param arg Аргумент рядка (вигляд --name[=value]).
 * @return Код, повернений обробником (0 — успіх).
 */
static int argp_handle_option (arg_parser_t *p, const arg_option_t *opt, const char *arg) {
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
    (void)p;
    return opt->handler (value);
}

/**
 * @brief Розбирає один аргумент: знаходить опцію або викликає default.
 * @param p Парсер.
 * @param arg Аргумент.
 * @return 0 — успіх, -1 — помилка/невідома опція.
 */
static int argp_parse_one (arg_parser_t *p, const char *arg) {
    for (size_t i = 0; i < p->count; i++) {
        arg_option_t *opt = &p->options[i];
        if (argp_matches_long (opt, arg) || argp_matches_short (opt, arg)) {
            return argp_handle_option (p, opt, arg);
        }
    }
    if (p->default_handler)
        return p->default_handler (arg);
    fprintf (stderr, "Невідомий аргумент: %s\n", arg);
    return -1;
}

/**
 * @brief Розбирає масив аргументів; підтримує --help/-h для друку usage.
 * @param p Парсер.
 * @param argc Кількість аргументів.
 * @param argv Масив аргументів.
 * @return 0 — успіх, -1 — помилка.
 */
int argp_arg_parser_parse (arg_parser_t *p, int argc, const char *argv[]) {
    if (!p || argc < 0)
        return -1;
    p->program = (argc > 0) ? argv[0] : p->program;
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp (arg, "--help") == 0 || strcmp (arg, "-h") == 0) {
            argp_print_usage_header (p, stdout);
            argp_arg_parser_print_options (p, stdout);
            p->printed_usage = 1;
            return 0;
        }
        if (argp_parse_one (p, arg) != 0)
            return -1;
    }
    return 0;
}
