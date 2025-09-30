/**
 * @file help.c
 * @brief Автоматична генерація довідки CLI.
 * @ingroup help
 * @details
 * Формує текст довідки на основі описів команд і опцій з `args.c`. Містить
 * внутрішні допоміжні структури і функції для рендеру таблиць параметрів,
 * секцій команд і службових заголовків.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "args.h"
#include "help.h"
#include "proginfo.h"

/**
 * @brief Друкує загальний опис призначення програми.
 */
static void help_cli_description (void) {
    fprintf (stdout, "Опис:\n");
    fprintf (
        stdout, "  cplot виконує локальні команди (print, device, config тощо) без постійного\n"
                "  фонового процесу. Перед друком або зміною налаштувань запустіть\n"
                "  `cplot device profile`, щоб визначити активний порт AxiDraw та оновити\n"
                "  розміри паперу і швидкості руху.\n\n");
}

/**
 * @brief Відображення ключа групи опцій у локалізований заголовок.
 */
typedef struct option_group_title {
    const char *key;   /**< Ключ групи (internal id). */
    const char *title; /**< Локалізований заголовок. */
} option_group_title_t;

/**
 * @brief Таблиця локалізованих заголовків груп опцій.
 */
static const option_group_title_t k_option_titles[] = {
    { "global", "Загальні параметри" },
    { "layout", "Параметри розкладки" },
    { "device-settings", "Налаштування команди device" },
    { "font", "Опції команди font" },
    { "config", "Параметри конфігурації" },

};

/** \brief Максимальна кількість груп опцій, які можуть бути надруковані. */
#define MAX_OPTION_GROUPS 16

/**
 * @brief Оцінює довжину текстової мітки опції для вирівнювання колонок.
 * @param desc Опис опції.
 * @return Кількість символів мітки "-s, --long <ARG>".
 */
static size_t help_option_label_length (const cli_option_desc_t *desc) {
    size_t len = 0;
    if (desc->short_name != '\0')
        len += 4;
    else
        len += 4;

    len += 2;
    if (desc->long_name)
        len += strlen (desc->long_name);

    if (desc->arg_placeholder && desc->arg_placeholder[0] != '\0')
        len += 3 + strlen (desc->arg_placeholder);

    return len;
}

/**
 * @brief Форматує мітку опції у буфер.
 * @param desc Опис опції.
 * @param buf [out] Вихідний буфер (C‑рядок).
 * @param buf_size Розмір буфера у байтах (включно з нуль-термінатором).
 */
static void help_format_option_label (const cli_option_desc_t *desc, char *buf, size_t buf_size) {
    size_t written = 0;
    if (desc->short_name != '\0') {
        if (written < buf_size) {
            size_t space = buf_size - written;
            if (space > 0)
                snprintf (buf + written, space, "-%c, ", desc->short_name);
        }
        written += 4;
    } else {
        if (written < buf_size) {
            size_t space = buf_size - written;
            if (space > 0)
                snprintf (buf + written, space, "    ");
        }
        written += 4;
    }

    const char *long_name = desc->long_name ? desc->long_name : "";
    size_t long_len = 2 + strlen (long_name);
    if (written < buf_size) {
        size_t space = buf_size - written;
        if (space > 0)
            snprintf (buf + written, space, "--%s", long_name);
    }
    written += long_len;

    if (desc->arg_placeholder && desc->arg_placeholder[0] != '\0') {
        size_t placeholder_len = strlen (desc->arg_placeholder);
        if (written < buf_size) {
            size_t space = buf_size - written;
            if (space > 0)
                snprintf (buf + written, space, " <%s>", desc->arg_placeholder);
        }
        written += 3 + placeholder_len;
    }

    if (buf_size > 0) {
        size_t idx = written < buf_size ? written : buf_size - 1;
        buf[idx] = '\0';
    }
}

/**
 * @brief Повертає локалізований заголовок для ключа групи.
 * @param group_key Ключ групи або `NULL`/порожній.
 * @return Рядок заголовка; для невідомих ключів — сам ключ.
 */
static const char *help_lookup_group_title (const char *group_key) {
    if (!group_key || group_key[0] == '\0')
        return "Інші параметри";
    for (size_t i = 0; i < sizeof (k_option_titles) / sizeof (k_option_titles[0]); ++i) {
        if (strcmp (group_key, k_option_titles[i].key) == 0)
            return k_option_titles[i].title;
    }
    return group_key;
}

/**
 * @brief Додає ключ групи до множини вже надрукованих.
 * @param group_key Ключ групи (може бути `NULL` — трактує як порожній).
 * @param printed Масив покажчиків на ключі (накопичувач).
 * @param printed_count Лічильник елементів у `printed` (буде оновлено).
 * @return 1 — додано новий ключ; 0 — ключ уже був присутній.
 */
static int help_add_group_marker (const char *group_key, const char **printed, size_t *printed_count) {
    if (!group_key)
        group_key = "";
    for (size_t i = 0; i < *printed_count; ++i) {
        if (strcmp (group_key, printed[i]) == 0)
            return 0;
    }
    if (*printed_count < MAX_OPTION_GROUPS) {
        printed[*printed_count] = group_key;
        (*printed_count)++;
    }
    return 1;
}

/**
 * @brief Перевіряє, чи група вже була надрукована.
 * @param group_key Ключ групи або `NULL` (трактує як порожній).
 * @param printed Масив уже надрукованих ключів.
 * @param printed_count Кількість елементів у `printed`.
 * @return 1 — вже надрукована; 0 — ще ні.
 */
static int
help_group_already_printed (const char *group_key, const char **printed, size_t printed_count) {
    if (!group_key)
        group_key = "";
    for (size_t i = 0; i < printed_count; ++i) {
        if (strcmp (group_key, printed[i]) == 0)
            return 1;
    }
    return 0;
}

/**
 * @brief Друкує пробіли для відступу.
 * @param stream Потік виводу.
 * @param indent Кількість пробілів.
 */
static void help_print_indent (FILE *stream, unsigned indent) {
    for (unsigned i = 0; i < indent; ++i)
        fputc (' ', stream);
}

/**
 * @brief Друкує один рядок опції: вирівняна мітка + опис.
 * @param desc Опис опції.
 * @param label_width Ширина колонки мітки.
 * @param stream Потік виводу.
 * @param indent Відступ зліва у пробілах.
 */
static void help_print_option_entry (
    const cli_option_desc_t *desc, size_t label_width, FILE *stream, unsigned indent) {
    char label[128];
    help_format_option_label (desc, label, sizeof (label));
    help_print_indent (stream, indent);
    fprintf (stream, "%-*s %s\n", (int)label_width, label, desc->description);
}

/**
 * @brief Налаштування рендеру однієї групи опцій.
 */
typedef struct option_group_render {
    const char *group_key;      /**< Ключ групи для відбору опцій. */
    const char *title_override; /**< Явний заголовок або `NULL` для lookup. */
    unsigned heading_indent;    /**< Відступ для заголовка. */
    unsigned entry_indent;      /**< Відступ для рядків опцій. */
} option_group_render_t;

/**
 * @brief Друкує групу опцій, що відповідають `render->group_key`.
 * @param render Налаштування рендеру.
 * @param options Масив описів опцій.
 * @param count Кількість опцій у масиві.
 * @param label_width Ширина колонки мітки.
 * @param stream Потік виводу.
 * @return true — щось надруковано; false — у групі немає опцій.
 */
static bool help_emit_option_group (
    const option_group_render_t *render,
    const cli_option_desc_t *options,
    size_t count,
    size_t label_width,
    FILE *stream) {
    if (!options || count == 0)
        return false;

    const char *target_group = render->group_key ? render->group_key : "";
    const char *title
        = render->title_override ? render->title_override : help_lookup_group_title (target_group);
    bool header_printed = false;

    for (size_t i = 0; i < count; ++i) {
        const cli_option_desc_t *desc = &options[i];
        const char *option_group = desc->group ? desc->group : "";
        if (strcmp (option_group, target_group) != 0)
            continue;
        if (!header_printed) {
            help_print_indent (stream, render->heading_indent);
            fprintf (stream, "%s:\n", title);
            header_printed = true;
        }
        help_print_option_entry (desc, label_width, stream, render->entry_indent);
    }

    if (header_printed)
        fprintf (stream, "\n");

    return header_printed;
}

/**
 * @brief Обчислює максимальну ширину колонки міток для вирівнювання.
 * @param options Масив описів опцій.
 * @param count Кількість елементів у масиві.
 * @return Максимальна довжина текстової мітки.
 */
static size_t help_compute_label_width (const cli_option_desc_t *options, size_t count) {
    if (!options || count == 0)
        return 0;
    size_t width = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t len = help_option_label_length (&options[i]);
        if (len > width)
            width = len;
    }
    return width;
}

/**
 * @brief Друкує секцію глобальних опцій.
 */
static void help_cli_global_options (
    const cli_option_desc_t *options,
    size_t option_count,
    size_t label_width,
    const char **printed_groups,
    size_t *printed_count) {
    option_group_render_t render
        = { "global", "Глобальні параметри (для будь-якої команди)", 0, 2 };
    if (help_emit_option_group (&render, options, option_count, label_width, stdout))
        help_add_group_marker (render.group_key, printed_groups, printed_count);
}

/**
 * @brief Відповідність команди та групи опцій для секцій у довідці.
 */
typedef struct command_option_section {
    const char *command;   /**< Імʼя команди. */
    const char *group_key; /**< Ключ групи опцій. */
    const char *title;     /**< Заголовок секції. */
} command_option_section_t;

/** \brief Перелік секцій опцій, згрупованих за командами. */
static const command_option_section_t k_command_sections[] = {
    { "print", "layout", "Параметри розкладки" },
    { "device", "device-settings", "Налаштування перед виконанням дій" },
    { "font", "font", "Опції команди font" },
    { "config", "config", "Опції команди config" },
};

/**
 * @brief Опис доступної дії команди `device`.
 */
typedef struct device_action_desc {
    const char *syntax;      /**< Синтаксис (коротка форма виклику). */
    const char *description; /**< Локалізований опис. */
} device_action_desc_t;

/** \brief Таблиця доступних дій для `device`. */
static const device_action_desc_t k_device_actions[] = {
    { "profile", "Підібрати профіль для активного пристрою та оновити локальні параметри" },
    { "list", "Перелічити підключені AxiDraw-пристрої" },
    { "pen up|down|toggle", "Керування станом пера" },
    { "motors on|off", "Увімкнути або вимкнути крокові двигуни" },
    { "jog [--dx <мм>] [--dy <мм>]", "Ручний зсув по осях у міліметрах" },
    { "status", "Поточний стан пристрою" },
    { "position", "Зняти координати з енкодерів" },
    { "home", "Повернутися у базову позицію" },
    { "reset", "Скинути контролер без втрати живлення" },
    { "reboot", "Перезавантажити контролер" },
    { "abort", "Негайно зупинити виконання команди" },
    { "version", "Вивести прошивку та сумісність" },
};

/**
 * @brief Друкує таблицю доступних дій команди `device`.
 * @param stream Потік виводу.
 */
static void help_print_device_actions (FILE *stream) {
    size_t max_len = 0;
    for (size_t i = 0; i < sizeof (k_device_actions) / sizeof (k_device_actions[0]); ++i) {
        size_t len = strlen (k_device_actions[i].syntax);
        if (len > max_len)
            max_len = len;
    }

    help_print_indent (stream, 4);
    fprintf (stream, "Доступні дії:\n");
    for (size_t i = 0; i < sizeof (k_device_actions) / sizeof (k_device_actions[0]); ++i) {
        help_print_indent (stream, 6);
        fprintf (
            stream, "%-*s %s\n", (int)max_len, k_device_actions[i].syntax,
            k_device_actions[i].description);
    }
    fprintf (stream, "\n");
}

/**
 * @brief Друкує перелік ключів для `config --set` з описами.
 * @param stream Потік виводу.
 * @param heading_indent Відступ для заголовка.
 * @param entry_indent Відступ для рядків переліку.
 */
static void help_print_config_keys (FILE *stream, unsigned heading_indent, unsigned entry_indent) {
    size_t key_count = 0;
    const cli_config_desc_t *keys = args_argdefs_config_keys (&key_count);
    if (!keys || key_count == 0)
        return;

    size_t max_key = 0;
    for (size_t i = 0; i < key_count; ++i) {
        size_t len = strlen (keys[i].key);
        if (len > max_key)
            max_key = len;
    }

    help_print_indent (stream, heading_indent);
    fprintf (stream, "Ключі для `config --set` (key=value):\n");
    for (size_t i = 0; i < key_count; ++i) {
        const cli_config_desc_t *desc = &keys[i];
        help_print_indent (stream, entry_indent);
        fprintf (stream, "%-*s %s\n", (int)max_key, desc->key, desc->description);
    }
    fprintf (stream, "\n");
}

/**
 * @brief Друкує секцію "Команди" з їхніми описами та повʼязаними опціями.
 */
static void help_cli_commands (
    const cli_option_desc_t *options,
    size_t option_count,
    size_t label_width,
    const char **printed_groups,
    size_t *printed_count) {
    size_t command_count = 0;
    const cli_command_desc_t *commands = args_argdefs_commands (&command_count);
    if (!commands || command_count == 0)
        return;

    fprintf (stdout, "Команди:\n");
    for (size_t i = 0; i < command_count; ++i) {
        const cli_command_desc_t *cmd = &commands[i];
        fprintf (stdout, "  %s - %s\n", cmd->name, cmd->description);

        if (strcmp (cmd->name, "device") == 0)
            help_print_device_actions (stdout);

        bool sections_printed = false;
        for (size_t j = 0; j < sizeof (k_command_sections) / sizeof (k_command_sections[0]); ++j) {
            const command_option_section_t *section = &k_command_sections[j];
            if (strcmp (section->command, cmd->name) != 0)
                continue;

            option_group_render_t render = { section->group_key, section->title, 4, 6 };
            if (help_emit_option_group (&render, options, option_count, label_width, stdout)) {
                help_add_group_marker (render.group_key, printed_groups, printed_count);
                sections_printed = true;
            }
        }

        if (strcmp (cmd->name, "config") == 0)
            help_print_config_keys (stdout, 4, 6);

        if (!sections_printed && strcmp (cmd->name, "config") != 0
            && strcmp (cmd->name, "device") != 0)
            fprintf (stdout, "\n");
    }
}

/**
 * @brief Друкує групи опцій, що не були привʼязані до конкретних команд.
 */
static void help_cli_remaining_groups (
    const cli_option_desc_t *options,
    size_t option_count,
    size_t label_width,
    const char **printed_groups,
    size_t *printed_count) {
    if (!options || option_count == 0)
        return;
    for (size_t i = 0; i < option_count; ++i) {
        const char *group_key = options[i].group ? options[i].group : "";
        if (help_group_already_printed (group_key, printed_groups, *printed_count))
            continue;

        option_group_render_t render = { group_key, NULL, 0, 2 };
        if (help_emit_option_group (&render, options, option_count, label_width, stdout))
            help_add_group_marker (group_key, printed_groups, printed_count);
    }
}

/**
 * @brief Друкує інформацію про автора програми.
 */
static void help_cli_author (void) { fprintf (stdout, "Автор: %s\n\n", __PROGRAM_AUTHOR__); }

/**
 * @copydoc help_cli_print_version
 */
void help_cli_print_version (void) {
    fprintf (stdout, "%s версія %s\n", __PROGRAM_NAME__, __PROGRAM_VERSION__);
}

/**
 * @copydoc help_cli_usage
 */
void help_cli_usage (void) {
    fprintf (stdout, "Використання:\n");
    fprintf (stdout, "  %s [ЗАГАЛЬНІ ПАРАМЕТРИ] КОМАНДА [АРГУМЕНТИ...]\n\n", __PROGRAM_NAME__);
}

/**
 * @copydoc help_cli_help
 */
void help_cli_help (void) {
    size_t option_count = 0;
    const cli_option_desc_t *options = args_argdefs_options (&option_count);
    size_t label_width = help_compute_label_width (options, option_count);
    const char *printed_groups[MAX_OPTION_GROUPS] = { 0 };
    size_t printed_count = 0;

    help_cli_usage ();
    help_cli_description ();
    if (options && option_count > 0)
        help_cli_global_options (options, option_count, label_width, printed_groups, &printed_count);
    help_cli_commands (options, option_count, label_width, printed_groups, &printed_count);
    if (options && option_count > 0)
        help_cli_remaining_groups (options, option_count, label_width, printed_groups, &printed_count);
    help_cli_author ();
}
