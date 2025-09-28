/**
 * @file help.c
 * @brief Help text for the client-side CLI.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "args.h"
#include "help.h"
#include "proginfo.h" /* Needed for __PROGRAM_NAME__, __PROGRAM_VERSION__, __PROGRAM_AUTHOR__ */

static void cli_description (void) {
    fprintf (stdout, "Опис:\n");
    fprintf (
        stdout, "  cplot виконує локальні команди (print, device, config тощо) без постійного\n"
                "  фонового процесу. Перед друком або зміною налаштувань запустіть\n"
                "  `cplot device profile`, щоб визначити активний порт AxiDraw та оновити\n"
                "  розміри паперу і швидкості руху.\n\n");
}

typedef struct option_group_title {
    const char *key;
    const char *title;
} option_group_title_t;

static const option_group_title_t k_option_titles[] = {
    { "global", "Загальні параметри" },
    { "layout", "Параметри розкладки" },
    { "device-settings", "Налаштування команди device" },
    { "font", "Опції команди font" },
    { "config", "Параметри конфігурації" },
    /* Підкоманда shape відсутня у CLI */
};

#define MAX_OPTION_GROUPS 16

static size_t option_label_length (const cli_option_desc_t *desc) {
    size_t len = 0;
    if (desc->short_name != '\0')
        len += 4; // "-x, "
    else
        len += 4; // відступ для вирівнювання

    len += 2; // префікс "--"
    if (desc->long_name)
        len += strlen (desc->long_name);

    if (desc->arg_placeholder && desc->arg_placeholder[0] != '\0')
        len += 3 + strlen (desc->arg_placeholder); // пробіл + <...>

    return len;
}

static void format_option_label (const cli_option_desc_t *desc, char *buf, size_t buf_size) {
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

static const char *lookup_group_title (const char *group_key) {
    if (!group_key || group_key[0] == '\0')
        return "Інші параметри";
    for (size_t i = 0; i < sizeof (k_option_titles) / sizeof (k_option_titles[0]); ++i) {
        if (strcmp (group_key, k_option_titles[i].key) == 0)
            return k_option_titles[i].title;
    }
    return group_key;
}

static int add_group_marker (const char *group_key, const char **printed, size_t *printed_count) {
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

static int
group_already_printed (const char *group_key, const char **printed, size_t printed_count) {
    if (!group_key)
        group_key = "";
    for (size_t i = 0; i < printed_count; ++i) {
        if (strcmp (group_key, printed[i]) == 0)
            return 1;
    }
    return 0;
}

static void print_indent (FILE *stream, unsigned indent) {
    for (unsigned i = 0; i < indent; ++i)
        fputc (' ', stream);
}

static void print_option_entry (
    const cli_option_desc_t *desc, size_t label_width, FILE *stream, unsigned indent) {
    char label[128];
    format_option_label (desc, label, sizeof (label));
    print_indent (stream, indent);
    fprintf (stream, "%-*s %s\n", (int)label_width, label, desc->description);
}

typedef struct option_group_render {
    const char *group_key;
    const char *title_override;
    unsigned heading_indent;
    unsigned entry_indent;
} option_group_render_t;

static bool emit_option_group (
    const option_group_render_t *render,
    const cli_option_desc_t *options,
    size_t count,
    size_t label_width,
    FILE *stream) {
    if (!options || count == 0)
        return false;

    const char *target_group = render->group_key ? render->group_key : "";
    const char *title
        = render->title_override ? render->title_override : lookup_group_title (target_group);
    bool header_printed = false;

    for (size_t i = 0; i < count; ++i) {
        const cli_option_desc_t *desc = &options[i];
        const char *option_group = desc->group ? desc->group : "";
        if (strcmp (option_group, target_group) != 0)
            continue;
        if (!header_printed) {
            print_indent (stream, render->heading_indent);
            fprintf (stream, "%s:\n", title);
            header_printed = true;
        }
        print_option_entry (desc, label_width, stream, render->entry_indent);
    }

    if (header_printed)
        fprintf (stream, "\n");

    return header_printed;
}

static size_t compute_label_width (const cli_option_desc_t *options, size_t count) {
    if (!options || count == 0)
        return 0;
    size_t width = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t len = option_label_length (&options[i]);
        if (len > width)
            width = len;
    }
    return width;
}

static void cli_global_options (
    const cli_option_desc_t *options,
    size_t option_count,
    size_t label_width,
    const char **printed_groups,
    size_t *printed_count) {
    option_group_render_t render
        = { "global", "Глобальні параметри (для будь-якої команди)", 0, 2 };
    if (emit_option_group (&render, options, option_count, label_width, stdout))
        add_group_marker (render.group_key, printed_groups, printed_count);
}

typedef struct command_option_section {
    const char *command;
    const char *group_key;
    const char *title;
} command_option_section_t;

static const command_option_section_t k_command_sections[] = {
    { "print", "layout", "Параметри розкладки" },
    { "device", "device-settings", "Налаштування перед виконанням дій" },
    { "font", "font", "Опції команди font" },
    { "config", "config", "Опції команди config" },
};

typedef struct device_action_desc {
    const char *syntax;
    const char *description;
} device_action_desc_t;

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

static void print_device_actions (FILE *stream) {
    size_t max_len = 0;
    for (size_t i = 0; i < sizeof (k_device_actions) / sizeof (k_device_actions[0]); ++i) {
        size_t len = strlen (k_device_actions[i].syntax);
        if (len > max_len)
            max_len = len;
    }

    print_indent (stream, 4);
    fprintf (stream, "Доступні дії:\n");
    for (size_t i = 0; i < sizeof (k_device_actions) / sizeof (k_device_actions[0]); ++i) {
        print_indent (stream, 6);
        fprintf (
            stream, "%-*s %s\n", (int)max_len, k_device_actions[i].syntax,
            k_device_actions[i].description);
    }
    fprintf (stream, "\n");
}

/* Блок підкоманди shape відсутній у CLI */

static void print_config_keys (FILE *stream, unsigned heading_indent, unsigned entry_indent) {
    size_t key_count = 0;
    const cli_config_desc_t *keys = argdefs_config_keys (&key_count);
    if (!keys || key_count == 0)
        return;

    size_t max_key = 0;
    for (size_t i = 0; i < key_count; ++i) {
        size_t len = strlen (keys[i].key);
        if (len > max_key)
            max_key = len;
    }

    print_indent (stream, heading_indent);
    fprintf (stream, "Ключі для `config --set` (key=value):\n");
    for (size_t i = 0; i < key_count; ++i) {
        const cli_config_desc_t *desc = &keys[i];
        print_indent (stream, entry_indent);
        fprintf (stream, "%-*s %s\n", (int)max_key, desc->key, desc->description);
    }
    fprintf (stream, "\n");
}

static void cli_commands (
    const cli_option_desc_t *options,
    size_t option_count,
    size_t label_width,
    const char **printed_groups,
    size_t *printed_count) {
    size_t command_count = 0;
    const cli_command_desc_t *commands = argdefs_commands (&command_count);
    if (!commands || command_count == 0)
        return;

    fprintf (stdout, "Команди:\n");
    for (size_t i = 0; i < command_count; ++i) {
        const cli_command_desc_t *cmd = &commands[i];
        fprintf (stdout, "  %s - %s\n", cmd->name, cmd->description);

        if (strcmp (cmd->name, "device") == 0)
            print_device_actions (stdout);

        bool sections_printed = false;
        for (size_t j = 0; j < sizeof (k_command_sections) / sizeof (k_command_sections[0]); ++j) {
            const command_option_section_t *section = &k_command_sections[j];
            if (strcmp (section->command, cmd->name) != 0)
                continue;

            option_group_render_t render = { section->group_key, section->title, 4, 6 };
            if (emit_option_group (&render, options, option_count, label_width, stdout)) {
                add_group_marker (render.group_key, printed_groups, printed_count);
                sections_printed = true;
            }
        }

        if (strcmp (cmd->name, "config") == 0)
            print_config_keys (stdout, 4, 6);

        if (!sections_printed && strcmp (cmd->name, "config") != 0
            && strcmp (cmd->name, "device") != 0)
            fprintf (stdout, "\n");
    }
}

static void cli_remaining_groups (
    const cli_option_desc_t *options,
    size_t option_count,
    size_t label_width,
    const char **printed_groups,
    size_t *printed_count) {
    if (!options || option_count == 0)
        return;
    for (size_t i = 0; i < option_count; ++i) {
        const char *group_key = options[i].group ? options[i].group : "";
        if (group_already_printed (group_key, printed_groups, *printed_count))
            continue;

        option_group_render_t render = { group_key, NULL, 0, 2 };
        if (emit_option_group (&render, options, option_count, label_width, stdout))
            add_group_marker (group_key, printed_groups, printed_count);
    }
}

static void cli_author (void) { fprintf (stdout, "Автор: %s\n\n", __PROGRAM_AUTHOR__); }

void cli_print_version (void) {
    fprintf (stdout, "%s версія %s\n", __PROGRAM_NAME__, __PROGRAM_VERSION__);
}

void cli_usage (void) {
    fprintf (stdout, "Використання:\n");
    fprintf (stdout, "  %s [ЗАГАЛЬНІ ПАРАМЕТРИ] КОМАНДА [АРГУМЕНТИ...]\n\n", __PROGRAM_NAME__);
}

void cli_help (void) {
    size_t option_count = 0;
    const cli_option_desc_t *options = argdefs_options (&option_count);
    size_t label_width = compute_label_width (options, option_count);
    const char *printed_groups[MAX_OPTION_GROUPS] = { 0 };
    size_t printed_count = 0;

    cli_usage ();
    cli_description ();
    if (options && option_count > 0)
        cli_global_options (options, option_count, label_width, printed_groups, &printed_count);
    cli_commands (options, option_count, label_width, printed_groups, &printed_count);
    if (options && option_count > 0)
        cli_remaining_groups (options, option_count, label_width, printed_groups, &printed_count);
    cli_author ();
}
