/**
 * @file args.c
 * @brief Implementation of command-line argument parsing for cplot.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "config.h"
#include "log.h"
/* Допоміжні кольори/довідка не потрібні для парсингу; залишаємо цей модуль сфокусованим. */

/**
 * Безпечно скопіювати рядок у фіксований буфер з гарантією NUL-термінатора.
 */
static void copy_string (char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0)
        return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy (dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

/**
 * Ініціалізувати всі опції типовими значеннями.
 *
 * @param options Вказівник на структуру опцій для ініціалізації (не NULL).
 */
static void set_default_options (options_t *options) {
    options->help = false;
    options->version = false;
    options->use_colors = true;
    options->cmd = CMD_NONE;
    memset (options->file_name, 0, sizeof (options->file_name));

    options->orientation = ORIENT_PORTRAIT; /* типово: портрет */
    options->margin_top_mm = 0.0;
    options->margin_right_mm = 0.0;
    options->margin_bottom_mm = 0.0;
    options->margin_left_mm = 0.0;
    options->paper_w_mm = 0.0;
    options->paper_h_mm = 0.0;
    options->preview = false;
    options->preview_png = false;
    options->dry_run = false;
    options->verbose = false;
    options->font_family[0] = '\0';
    options->device_model[0] = '\0';
    options->device_action = DEV_NONE;
    options->device_port[0] = '\0';
    options->jog_dx_mm = 0.0;
    options->jog_dy_mm = 0.0;
    options->config_action = CFG_NONE;
    options->config_set_pairs[0] = '\0';
}

/**
 * Централізовані визначення опцій/команд CLI (раніше у argdefs.c)
 * Використовуються як парсером, так і модулем help.c
 */
static const struct option k_long_options[] = {
    { "help", no_argument, 0, 'h' },
    { "version", no_argument, 0, 'v' },
    { "no-colors", no_argument, 0, ARG_NO_COLORS },
    { "portrait", no_argument, 0, ARG_PORTRAIT },
    { "landscape", no_argument, 0, ARG_LANDSCAPE },
    { "margins", required_argument, 0, ARG_MARGINS },
    { "paper-w", required_argument, 0, ARG_PAPER_W },
    { "paper-h", required_argument, 0, ARG_PAPER_H },
    { "png", no_argument, 0, ARG_PNG },
    { "preview", no_argument, 0, ARG_PREVIEW },
    { "dry-run", no_argument, 0, ARG_DRY_RUN },
    { "verbose", no_argument, 0, ARG_VERBOSE },
    { "list", no_argument, 0, ARG_LIST },
    { "port", required_argument, 0, ARG_PORT },
    { "dx", required_argument, 0, ARG_DX },
    { "dy", required_argument, 0, ARG_DY },
    { "family", required_argument, 0, ARG_FAMILY },
    { "show", no_argument, 0, ARG_SHOW },
    { "reset", no_argument, 0, ARG_RESET },
    { "set", required_argument, 0, ARG_SET },
    { "device", required_argument, 0, ARG_DEVICE_MODEL },
    { 0, 0, 0, 0 },
};

const struct option *argdefs_long_options (void) { return k_long_options; }

static const cli_option_desc_t k_option_descs[] = {
    { "help", no_argument, 'h', 'h', NULL, "global", "Показати довідку" },
    { "version", no_argument, 'v', 'v', NULL, "global", "Показати версію" },
    { "no-colors", no_argument, ARG_NO_COLORS, '\0', NULL, "global", "Вимкнути ANSI-кольори" },
    { "device", required_argument, ARG_DEVICE_MODEL, '\0', "name", "global",
      "Обрати пристрій (напр., minikit2)" },
    { "family", required_argument, ARG_FAMILY, '\0', "name", "global",
      "Родина шрифтів (напр., hershey_sans_med)" },
    { "verbose", no_argument, ARG_VERBOSE, '\0', NULL, "global", "Розгорнутий вивід" },
    { "dry-run", no_argument, ARG_DRY_RUN, '\0', NULL, "global",
      "Не надсилати команди на пристрій" },

    { "portrait", no_argument, ARG_PORTRAIT, '\0', NULL, "layout", "Орієнтація портрет" },
    { "landscape", no_argument, ARG_LANDSCAPE, '\0', NULL, "layout", "Орієнтація альбомна" },
    { "margins", required_argument, ARG_MARGINS, '\0', "T[,R,B,L]", "layout",
      "Поля у мм (T[,R,B,L])" },
    { "paper-w", required_argument, ARG_PAPER_W, '\0', "мм", "layout", "Ширина паперу" },
    { "paper-h", required_argument, ARG_PAPER_H, '\0', "мм", "layout", "Висота паперу" },
    { "preview", no_argument, ARG_PREVIEW, '\0', NULL, "layout",
      "Не надсилати на пристрій; вивести прев’ю у stdout" },
    { "png", no_argument, ARG_PNG, '\0', NULL, "layout", "При --preview вивести PNG замість SVG" },

    { "list", no_argument, ARG_LIST, '\0', NULL, "device", "Перелічити доступні порти" },
    { "port", required_argument, ARG_PORT, '\0', "PATH", "device", "Вказати шлях до порту" },
    { "dx", required_argument, ARG_DX, '\0', "мм", "device", "Зсув по X для jog" },
    { "dy", required_argument, ARG_DY, '\0', "мм", "device", "Зсув по Y для jog" },

    { "show", no_argument, ARG_SHOW, '\0', NULL, "config", "Показати конфігурацію" },
    { "reset", no_argument, ARG_RESET, '\0', NULL, "config", "Скинути до типової" },
    { "set", required_argument, ARG_SET, '\0', "key=value[,..]", "config",
      "Задати ключі конфігурації" },
};

const cli_option_desc_t *argdefs_options (size_t *out_count) {
    if (out_count)
        *out_count = sizeof (k_option_descs) / sizeof (k_option_descs[0]);
    return k_option_descs;
}

static const cli_command_desc_t k_commands[] = {
    { "print",
      "Плотинг із параметрами розкладки (для прев’ю використовуйте --preview, SVG/PNG у stdout)" },
    { "device", "Утиліти пристрою (list, jog, pen)" },
    { "fonts", "Перелік доступних векторних шрифтів" },
    { "config", "Показати або змінити типові налаштування" },
    { "version", "Показати версію" },
    { "sysinfo", "Показати діагностичну інформацію" },
};

const cli_command_desc_t *argdefs_commands (size_t *out_count) {
    if (out_count)
        *out_count = sizeof (k_commands) / sizeof (k_commands[0]);
    return k_commands;
}

/* ---- Конфігураційні ключі (для секції help і валідації) ---- */
static const cli_config_desc_t k_cfg_keys[] = {
    { "orient", CFGK_ENUM, offsetof (config_t, orientation), NULL, "portrait|landscape",
      "Орієнтація сторінки", "%s" },
    { "paper_w", CFGK_DOUBLE, offsetof (config_t, paper_w_mm), "мм", NULL, "Ширина паперу",
      "%.1f" },
    { "paper_h", CFGK_DOUBLE, offsetof (config_t, paper_h_mm), "мм", NULL, "Висота паперу",
      "%.1f" },
    { "margin", CFGK_DOUBLE, offsetof (config_t, margin_top_mm), "мм", NULL,
      "Поля для всіх сторін (скорочення)", "%.1f" },
    { "margin_t", CFGK_DOUBLE, offsetof (config_t, margin_top_mm), "мм", NULL, "Верхнє поле",
      "%.1f" },
    { "margin_r", CFGK_DOUBLE, offsetof (config_t, margin_right_mm), "мм", NULL, "Праве поле",
      "%.1f" },
    { "margin_b", CFGK_DOUBLE, offsetof (config_t, margin_bottom_mm), "мм", NULL, "Нижнє поле",
      "%.1f" },
    { "margin_l", CFGK_DOUBLE, offsetof (config_t, margin_left_mm), "мм", NULL, "Ліве поле",
      "%.1f" },
    { "speed", CFGK_DOUBLE, offsetof (config_t, speed_mm_s), "мм_с", NULL, "Швидкість", "%.1f" },
    { "accel", CFGK_DOUBLE, offsetof (config_t, accel_mm_s2), "мм_с2", NULL, "Прискорення",
      "%.1f" },
    { "pen_up", CFGK_INT, offsetof (config_t, pen_up_pos), "%", NULL, "Підняте перо", "%d" },
    { "pen_down", CFGK_INT, offsetof (config_t, pen_down_pos), "%", NULL, "Опущене перо", "%d" },
    { "pen_up_speed", CFGK_INT, offsetof (config_t, pen_up_speed), "%/с", NULL,
      "Швидкість підйому пера", "%d" },
    { "pen_down_speed", CFGK_INT, offsetof (config_t, pen_down_speed), "%/с", NULL,
      "Швидкість опускання пера", "%d" },
    { "pen_up_delay", CFGK_INT, offsetof (config_t, pen_up_delay_ms), "мс", NULL,
      "Затримка після підйому", "%d" },
    { "pen_down_delay", CFGK_INT, offsetof (config_t, pen_down_delay_ms), "мс", NULL,
      "Затримка після опускання", "%d" },
    { "servo_timeout", CFGK_INT, offsetof (config_t, servo_timeout_s), "с", NULL,
      "Тайм-аут сервоприводу", "%d" },
};

const cli_config_desc_t *argdefs_config_keys (size_t *out_count) {
    if (out_count)
        *out_count = sizeof (k_cfg_keys) / sizeof (k_cfg_keys[0]);
    return k_cfg_keys;
}

/**
 * Обробити коротку або довгу опцію, повернену getopt_long.
 *
 * @param arg     Код опції, який повернув getopt_long (включно зі штучними кодами для long-опцій).
 * @param options Вказівник на структуру опцій для оновлення (не NULL).
 */
void switch_options (int arg, options_t *options) {
    switch (arg) {
    case 'h':
        options->help = true;
        LOGD ("отримано прапорець допомоги (-h|--help)");
        break;

    case 'v':
        options->version = true;
        LOGD ("отримано прапорець версії (-v|--version)");
        break;

    case 0:
        options->use_colors = false;
        LOGD ("вимкнено кольори ANSI через --no-colors");
        break;

    case '?':
        /* getopt уже вивів повідомлення про помилку */
        break;

    default:
        LOGW ("невідома опція: код=%d", arg);
        break;
    }
}

/**
 * Встановити ім'я вхідного файлу з позиційного аргументу або позначити stdin.
 *
 * Якщо після обробки опцій у argv лишився ще один позиційний аргумент, він
 * вважається шляхом до вхідного файлу. Інакше використовується "-" (stdin).
 *
 * @param argc    Кількість аргументів командного рядка.
 * @param argv    Масив аргументів командного рядка.
 * @param options Вказівник на структуру опцій для заповнення поля file_name.
 */
void get_file_name (int argc, char *argv[], options_t *options) {
    // Якщо є ще один аргумент — вважати його шляхом до вхідного файлу
    if (optind < argc) {
        copy_string (options->file_name, sizeof (options->file_name), argv[optind++]);
        LOGD ("вхідний файл: %s", options->file_name);
    } else {
        // Інакше вважати stdin вхідним файлом
        copy_string (options->file_name, sizeof (options->file_name), "-");
        LOGD ("вхідний файл: stdin (-)");
    }
}

/**
 * Розібрати аргументи командного рядка у структуру опцій (реентерабельна функція).
 *
 * Підтримує підкоманди (print, device, fonts, config, version, sysinfo) та набір
 * коротких/довгих опцій. На macOS додатково скидає стан getopt для повторних викликів.
 *
 * @param argc    Кількість аргументів командного рядка.
 * @param argv    Масив аргументів командного рядка.
 * @param options Вказівник на структуру, у яку будуть записані розібрані опції (не NULL).
 */
void options_parser (int argc, char *argv[], options_t *options) {
    set_default_options (options);

    int arg; /* Поточна опція */

// Скинути стан getopt для повторних викликів (тести викликають парсер багаторазово)
#ifdef __APPLE__
    extern int optreset;
    optreset = 1;
#endif
    optind = 1;

    // Розібрати підкоманду, якщо присутня (argv[1])
    if (argc >= 2 && argv[1][0] != '-') {
        if (strcmp (argv[1], "print") == 0)
            options->cmd = CMD_PRINT;
        else if (strcmp (argv[1], "device") == 0)
            options->cmd = CMD_DEVICE;
        else if (strcmp (argv[1], "fonts") == 0)
            options->cmd = CMD_FONTS;
        else if (strcmp (argv[1], "config") == 0)
            options->cmd = CMD_CONFIG;
        else if (strcmp (argv[1], "version") == 0)
            options->cmd = CMD_VERSION;
        else if (strcmp (argv[1], "sysinfo") == 0)
            options->cmd = CMD_SYSINFO;
        else
            options->cmd = CMD_NONE; // невідомо — нехай usage розкаже згодом

        if (options->cmd != CMD_NONE) {
            LOGD ("виявлено підкоманду: %d", options->cmd);
            // Просунутись повз підкоманду для getopt_long
            argv++;
            argc--;
        }
    }

    // Дозволені опції для getopt (централізовано в argdefs)
    const struct option *long_options = argdefs_long_options ();

    while (true) {

        int option_index = 0;
        arg = getopt_long (argc, argv, "hv", long_options, &option_index);

        // Кінець списку опцій?
        if (arg == -1)
            break;

        // Знайти відповідний випадок аргументу
        if (arg == 0) {
            // --no-colors (довга опція) потрапляє сюди, коли val==0
            options->use_colors = false;
            continue;
        }

        switch (arg) {
        case 1:
            options->orientation = ORIENT_PORTRAIT;
            LOGD ("орієнтація: portrait");
            break;
        case 2:
            options->orientation = ORIENT_LANDSCAPE;
            LOGD ("орієнтація: landscape");
            break;
        case 4: {
            // розібрати T,R,B,L:mm або просто число = всі сторони у мм
            double t = 0, r = 0, b = 0, l = 0;
            char *s = optarg;
            char *end = NULL;
            int parts = 0;
            char buf[256];
            strncpy (buf, s, sizeof (buf) - 1);
            buf[sizeof (buf) - 1] = '\0';
            char *token = strtok (buf, ",");
            while (token && parts < 4) {
                double v = strtod (token, &end);
                if (end == token)
                    break;
                if (parts == 0)
                    t = v;
                else if (parts == 1)
                    r = v;
                else if (parts == 2)
                    b = v;
                else
                    l = v;
                parts++;
                token = strtok (NULL, ",");
            }
            if (parts == 1) {
                r = b = l = t;
            }
            options->margin_top_mm = t;
            options->margin_right_mm = r;
            options->margin_bottom_mm = b;
            options->margin_left_mm = l;
            break;
        }
        case 5:
            options->paper_w_mm = atof (optarg);
            LOGD ("paper_w=%.3f мм", options->paper_w_mm);
            break;
        case 6:
            options->paper_h_mm = atof (optarg);
            LOGD ("paper_h=%.3f мм", options->paper_h_mm);
            break;
        case 8:
            options->preview_png = true;
            LOGD ("режим прев’ю: PNG");
            break;
        case 20:
            options->preview = true;
            LOGD ("увімкнено прев’ю (SVG за замовчуванням)");
            break;
        case 9:
            options->dry_run = true;
            LOGD ("сухий запуск: без надсилання на пристрій");
            break;
        case 10:
            options->verbose = true;
            LOGD ("детальний вивід (--verbose)");
            break;
        case 11:
            options->device_action = DEV_LIST;
            LOGD ("device: --list");
            break;
        case 12:
            copy_string (options->device_port, sizeof (options->device_port), optarg);
            break;
        case 13:
            options->jog_dx_mm = atof (optarg);
            break;
        case 14:
            options->jog_dy_mm = atof (optarg);
            break;
        case 15:
            copy_string (options->font_family, sizeof (options->font_family), optarg);
            LOGD ("font family: %s", options->font_family);
            break;
        case 16:
            options->config_action = CFG_SHOW;
            LOGD ("config: --show");
            break;
        case 17:
            options->config_action = CFG_RESET;
            LOGD ("config: --reset");
            break;
        case 18:
            options->config_action = CFG_SET;
            copy_string (options->config_set_pairs, sizeof (options->config_set_pairs), optarg);
            LOGD ("config: --set %s", options->config_set_pairs);
            break;
        case 19:
            copy_string (options->device_model, sizeof (options->device_model), optarg);
            LOGD ("device model: %s", options->device_model);
            break;
        default:
            switch_options (arg, options);
        }
    }

    /* Handle device positional action (e.g., up, down, jog). On some libc
     * (e.g., macOS), getopt_long stops at the first non-option token, so make a
     * robust pass to capture positional action and any dx/dy that follow it. */
    if (options->cmd == CMD_DEVICE) {
        // Prefer getopt result if it already set an action via --list
        if (options->device_action == DEV_NONE) {
            // If getopt stopped at first non-option, optind should point to it
            if (optind < argc) {
                const char *act = argv[optind];
                if (strcmp (act, "up") == 0)
                    options->device_action = DEV_UP;
                else if (strcmp (act, "down") == 0)
                    options->device_action = DEV_DOWN;
                else if (strcmp (act, "toggle") == 0)
                    options->device_action = DEV_TOGGLE;
                else if (strcmp (act, "motors-on") == 0)
                    options->device_action = DEV_MOTORS_ON;
                else if (strcmp (act, "motors-off") == 0)
                    options->device_action = DEV_MOTORS_OFF;
                else if (strcmp (act, "home") == 0)
                    options->device_action = DEV_HOME;
                else if (strcmp (act, "jog") == 0)
                    options->device_action = DEV_JOG;
                else if (strcmp (act, "version") == 0)
                    options->device_action = DEV_VERSION;
                else if (strcmp (act, "status") == 0)
                    options->device_action = DEV_STATUS;
                else if (strcmp (act, "position") == 0)
                    options->device_action = DEV_POSITION;
                else if (strcmp (act, "reset") == 0)
                    options->device_action = DEV_RESET;
                else if (strcmp (act, "reboot") == 0)
                    options->device_action = DEV_REBOOT;
            }
            // Як запасний варіант, проглянути всі токени без опцій на дію
            if (options->device_action == DEV_NONE) {
                for (int i = 1; i < argc; i++) {
                    if (argv[i][0] == '-')
                        continue;
                    if (strcmp (argv[i], "up") == 0) {
                        options->device_action = DEV_UP;
                        break;
                    } else if (strcmp (argv[i], "down") == 0) {
                        options->device_action = DEV_DOWN;
                        break;
                    } else if (strcmp (argv[i], "toggle") == 0) {
                        options->device_action = DEV_TOGGLE;
                        break;
                    } else if (strcmp (argv[i], "motors-on") == 0) {
                        options->device_action = DEV_MOTORS_ON;
                        break;
                    } else if (strcmp (argv[i], "motors-off") == 0) {
                        options->device_action = DEV_MOTORS_OFF;
                        break;
                    } else if (strcmp (argv[i], "home") == 0) {
                        options->device_action = DEV_HOME;
                        break;
                    } else if (strcmp (argv[i], "jog") == 0) {
                        options->device_action = DEV_JOG;
                        break;
                    } else if (strcmp (argv[i], "version") == 0) {
                        options->device_action = DEV_VERSION;
                        break;
                    } else if (strcmp (argv[i], "status") == 0) {
                        options->device_action = DEV_STATUS;
                        break;
                    } else if (strcmp (argv[i], "position") == 0) {
                        options->device_action = DEV_POSITION;
                        break;
                    } else if (strcmp (argv[i], "reset") == 0) {
                        options->device_action = DEV_RESET;
                        break;
                    } else if (strcmp (argv[i], "reboot") == 0) {
                        options->device_action = DEV_REBOOT;
                        break;
                    }
                }
                if (options->device_action != DEV_NONE)
                    LOGD ("device action: %d", options->device_action);
            }
        }

        // Розібрати дельти jog незалежно від порядку (підтримати токени після дії)
        for (int i = 1; i + 1 < argc; i++) {
            if (strcmp (argv[i], "--dx") == 0) {
                options->jog_dx_mm = atof (argv[i + 1]);
                i++;
                LOGD ("jog dx=%.3f мм", options->jog_dx_mm);
            } else if (strcmp (argv[i], "--dy") == 0) {
                options->jog_dy_mm = atof (argv[i + 1]);
                i++;
                LOGD ("jog dy=%.3f мм", options->jog_dy_mm);
            }
        }
    }

    // Отримати ім'я файлу або використати stdin; тільки для команд, що беруть файл
    if (options->cmd == CMD_PRINT || options->cmd == CMD_NONE) {
        get_file_name (argc, argv, options);
    }
}
