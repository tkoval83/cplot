/**
 * @file args.c
 * @brief Реалізація розбору аргументів командного рядка для cplot.
 *
 * Модуль інкапсулює всю логіку розбору CLI: карту довгих/коротких опцій,
 * опис команд для довідки, валідацію значень і побудову структури `options_t`.
 * Публічний інтерфейс зводиться до `options_parser`, яким користується main(),
 * та допоміжних функцій для довідки (`argdefs_*`).
 */
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "config.h"
#include "log.h"
#include "str.h"

/**
 * @brief Створити "порожню" дію пристрою.
 *
 * @return Структура з типом `DEVICE_ACTION_NONE` і скинутими полями.
 */
static device_action_t device_action_make_none (void) {
    device_action_t action
        = { .kind = DEVICE_ACTION_NONE, .pen = DEVICE_PEN_NONE, .motor = DEVICE_MOTOR_NONE };
    return action;
}

/**
 * @brief Створити дію пристрою без додаткових параметрів.
 *
 * @param kind Тип дії, яку слід підготувати.
 * @return Структура дії із заданим `kind`.
 */
static device_action_t device_action_make_simple (device_action_kind_t kind) {
    device_action_t action = device_action_make_none ();
    action.kind = kind;
    return action;
}

/**
 * @brief Створити дію, що стосується пера.
 *
 * @param pen Параметр руху пера (підняти, опустити тощо).
 * @return Структура дії з типом `DEVICE_ACTION_PEN`.
 */
static device_action_t device_action_make_pen (device_pen_action_t pen) {
    device_action_t action = device_action_make_simple (DEVICE_ACTION_PEN);
    action.pen = pen;
    return action;
}

/**
 * @brief Створити дію, що стосується моторів.
 *
 * @param motor Опис руху моторів (увімкнути/вимкнути).
 * @return Структура дії з типом `DEVICE_ACTION_MOTORS`.
 */
static device_action_t device_action_make_motor (device_motor_action_t motor) {
    device_action_t action = device_action_make_simple (DEVICE_ACTION_MOTORS);
    action.motor = motor;
    return action;
}

/**
 * @brief Перевірити, що дія встановлена.
 *
 * @param action Вказівник на дію, яку перевіряємо (може бути NULL).
 * @return `true`, якщо дія відмінна від `DEVICE_ACTION_NONE`.
 */
static bool device_action_is_set (const device_action_t *action) {
    return action && action->kind != DEVICE_ACTION_NONE;
}

/**
 * @brief Перетворити назву підкоманди CLI на внутрішній код.
 *
 * @param name Рядок із назвою підкоманди (`print`, `device`, `config` тощо).
 * @return Відповідне значення `cmd_t` або `CMD_NONE`, якщо назва не підтримується.
 */
static cmd_t parse_command_name (const char *name) {
    if (!name)
        return CMD_NONE;
    static const struct {
        const char *name;
        cmd_t cmd;
    } k_cmd_map[]
        = { { "print", CMD_PRINT }, { "device", CMD_DEVICE }, { "fonts", CMD_FONTS },
            { "font", CMD_FONTS },  { "config", CMD_CONFIG }, { "version", CMD_VERSION } };
    for (size_t i = 0; i < sizeof (k_cmd_map) / sizeof (k_cmd_map[0]); ++i) {
        if (strcmp (name, k_cmd_map[i].name) == 0)
            return k_cmd_map[i].cmd;
    }
    return CMD_NONE;
}

/**
 * @brief Перетворити текстовий токен на структуру дії пристрою.
 *
 * @param token Рядок позиційного аргументу (`list`, `pen-up`, `motors-off` тощо).
 * @return Відповідна структура `device_action_t` або `DEVICE_ACTION_NONE`, якщо токен невідомий.
 */
static device_action_t device_action_from_token (const char *token) {
    if (!token)
        return device_action_make_none ();

    if (strcmp (token, "list") == 0)
        return device_action_make_simple (DEVICE_ACTION_LIST);
    if (strcmp (token, "up") == 0 || strcmp (token, "pen-up") == 0)
        return device_action_make_pen (DEVICE_PEN_UP);
    if (strcmp (token, "down") == 0 || strcmp (token, "pen-down") == 0)
        return device_action_make_pen (DEVICE_PEN_DOWN);
    if (strcmp (token, "toggle") == 0 || strcmp (token, "pen-toggle") == 0)
        return device_action_make_pen (DEVICE_PEN_TOGGLE);
    if (strcmp (token, "motors-on") == 0)
        return device_action_make_motor (DEVICE_MOTOR_ON);
    if (strcmp (token, "motors-off") == 0)
        return device_action_make_motor (DEVICE_MOTOR_OFF);
    if (strcmp (token, "abort") == 0)
        return device_action_make_simple (DEVICE_ACTION_ABORT);
    if (strcmp (token, "home") == 0)
        return device_action_make_simple (DEVICE_ACTION_HOME);
    if (strcmp (token, "jog") == 0)
        return device_action_make_simple (DEVICE_ACTION_JOG);
    if (strcmp (token, "version") == 0)
        return device_action_make_simple (DEVICE_ACTION_VERSION);
    if (strcmp (token, "status") == 0)
        return device_action_make_simple (DEVICE_ACTION_STATUS);
    if (strcmp (token, "position") == 0)
        return device_action_make_simple (DEVICE_ACTION_POSITION);
    if (strcmp (token, "reset") == 0)
        return device_action_make_simple (DEVICE_ACTION_RESET);
    if (strcmp (token, "reboot") == 0)
        return device_action_make_simple (DEVICE_ACTION_REBOOT);
    if (strcmp (token, "profile") == 0)
        return device_action_make_simple (DEVICE_ACTION_PROFILE);

    return device_action_make_none ();
}

/**
 * @brief Проміжні результати розбору позиційних аргументів підкоманди device.
 */
typedef struct {
    device_action_t action;
    bool action_set;
    double jog_dx_mm;
    bool dx_set;
    double jog_dy_mm;
    bool dy_set;
} device_parse_result_t;

/**
 * @brief Розібрати позиційні токени підкоманди `device`.
 *
 * @param argc           Кількість аргументів у масиві `argv`.
 * @param argv           Масив аргументів, переданий у парсер.
 * @param current_optind Значення `optind`, з якого слід починати обробку позиційних токенів.
 * @param initial_action Початкова дія, виставлена опціями (`--list`, `--dx` тощо).
 * @return Структура з виявленою дією та параметрами `jog`.
 */
static device_parse_result_t
parse_device_tokens (int argc, char *argv[], int current_optind, device_action_t initial_action) {
    device_parse_result_t result = { .action = initial_action,
                                     .action_set = device_action_is_set (&initial_action),
                                     .jog_dx_mm = 0.0,
                                     .dx_set = false,
                                     .jog_dy_mm = 0.0,
                                     .dy_set = false };

    /* Послідовно обходимо всі позиційні токени, починаючи з current_optind (де getopt зупинився).
     * Це усуває попередню помилку, коли цикл починався з 1 і дублював аналіз ранніх аргументів. */
    for (int i = current_optind; i < argc; ++i) {
        const char *token = argv[i];
        if (!token || !*token)
            continue;

        /* Підтримка синтаксису з двох слів: "motors on|off" (як "pen up"). */
        if (strcmp (token, "motors") == 0) {
            const char *next = (i + 1 < argc) ? argv[i + 1] : NULL;
            if (next && next[0] != '-') {
                if (strcmp (next, "on") == 0) {
                    result.action = device_action_make_motor (DEVICE_MOTOR_ON);
                    result.action_set = true;
                    ++i; /* пропустити 'on' */
                    continue;
                }
                if (strcmp (next, "off") == 0) {
                    result.action = device_action_make_motor (DEVICE_MOTOR_OFF);
                    result.action_set = true;
                    ++i; /* пропустити 'off' */
                    continue;
                }
            }
            /* Якщо після 'motors' не йде on/off — продовжуємо розбір інших токенів. */
        }

        if (token[0] == '-') {
            /* Аналіз параметрів типу --dx VALUE / --dy VALUE (ігноруємо, якщо VALUE відсутнє). */
            if ((strcmp (token, "--dx") == 0) && i + 1 < argc) {
                const char *val = argv[i + 1];
                char *end = NULL;
                double v = strtod (val, &end);
                if (end != val && (!end || *end == '\0')) {
                    result.jog_dx_mm = v;
                    result.dx_set = true;
                } else {
                    LOGW ("ігнорую некоректне значення зсуву по X: '%s'", val);
                }
                ++i;
            } else if ((strcmp (token, "--dy") == 0) && i + 1 < argc) {
                const char *val = argv[i + 1];
                char *end = NULL;
                double v = strtod (val, &end);
                if (end != val && (!end || *end == '\0')) {
                    result.jog_dy_mm = v;
                    result.dy_set = true;
                } else {
                    LOGW ("ігнорую некоректне значення зсуву по Y: '%s'", val);
                }
                ++i;
            }
            continue;
        }

        /* Якщо дія ще не встановлена опцією або попереднім токеном — спробувати розпізнати. */
        if (!result.action_set) {
            device_action_t candidate = device_action_from_token (token);
            if (device_action_is_set (&candidate)) {
                result.action = candidate;
                result.action_set = true;
            }
        }
    }

    if (!result.action_set)
        result.action = device_action_make_none ();

    return result;
}

/**
 * @brief Скинути структуру `options_t` до типових значень.
 *
 * @param[out] options Структура, яку потрібно ініціалізувати (не NULL).
 */
/* Жодних значень за замовчуванням у парсері — лише нульове очищення для безпеки. */

/**
 * @brief Централізовані визначення довгих опцій для getopt_long().
 *
 * Частка аргументів використовується одночасно парсером і модулем help.c, тож
 * таблиця зберігається в одному місці, щоб уникнути розсинхронізації.
 */
static const struct option k_long_options[] = {
    { "help", no_argument, 0, 'h' },
    { "version", no_argument, 0, 'v' },
    { "no-colors", no_argument, 0, ARG_NO_COLORS },
    { "portrait", no_argument, 0, ARG_PORTRAIT },
    { "landscape", no_argument, 0, ARG_LANDSCAPE },
    { "margins", required_argument, 0, ARG_MARGINS },
    { "width", required_argument, 0, ARG_WIDTH },
    { "height", required_argument, 0, ARG_HEIGHT },
    { "png", no_argument, 0, ARG_PNG },
    { "output", required_argument, 0, ARG_OUTPUT },
    { "format", required_argument, 0, ARG_FORMAT },
    { "preview", no_argument, 0, ARG_PREVIEW },
    { "fit-page", no_argument, 0, ARG_FIT_PAGE },
    { "dry-run", no_argument, 0, ARG_DRY_RUN },
    { "verbose", no_argument, 0, ARG_VERBOSE },
    { "device-name", required_argument, 0, ARG_DEVICE_NAME },
    { "dx", required_argument, 0, ARG_DX },
    { "dy", required_argument, 0, ARG_DY },
    { "list", no_argument, 0, ARG_LIST },
    { "show", no_argument, 0, ARG_SHOW },
    { "reset", no_argument, 0, ARG_RESET },
    { "set", required_argument, 0, ARG_SET },
    { "device", required_argument, 0, ARG_DEVICE_MODEL },
    { "font-family", no_argument, 0, ARG_FONT_FAMILIES },
    { "family", required_argument, 0, ARG_FONT_FAMILY_VALUE },
    { 0, 0, 0, 0 },
};

/**
 * @brief Надати масив довгих опцій для зовнішніх користувачів (help.c).
 *
 * @return Статично розміщений масив `struct option`, життєвий цикл якого дорівнює програмі.
 */
const struct option *argdefs_long_options (void) { return k_long_options; }

/**
 * @brief Опис опцій з метаданими для довідкових таблиць.
 *
 * Використовується help.c та іншими компонентами для генерації читабельних
 * списків із категоріями, псевдонімами та прикладами значень.
 */
static const cli_option_desc_t k_option_descs[] = {
    { "help", no_argument, 'h', 'h', NULL, "global", "Показати довідку" },
    { "version", no_argument, 'v', 'v', NULL, "global", "Показати версію" },
    { "no-colors", no_argument, ARG_NO_COLORS, '\0', NULL, "global", "Вимкнути ANSI-кольори" },
    { "verbose", no_argument, ARG_VERBOSE, '\0', NULL, "global", "Розгорнутий вивід" },
    { "dry-run", no_argument, ARG_DRY_RUN, '\0', NULL, "global",
      "Не надсилати команди на пристрій" },

    { "device", required_argument, ARG_DEVICE_MODEL, '\0', "name", "config",
      "Обрати профіль пристрою (напр., minikit2)" },

    { "portrait", no_argument, ARG_PORTRAIT, '\0', NULL, "layout", "Орієнтація портрет" },
    { "landscape", no_argument, ARG_LANDSCAPE, '\0', NULL, "layout", "Орієнтація альбомна" },
    { "margins", required_argument, ARG_MARGINS, '\0', "T[,R,B,L]", "layout",
      "Поля у мм (T[,R,B,L])" },
    { "width", required_argument, ARG_WIDTH, '\0', "мм", "layout", "Ширина паперу" },
    { "height", required_argument, ARG_HEIGHT, '\0', "мм", "layout", "Висота паперу" },
    { "preview", no_argument, ARG_PREVIEW, '\0', NULL, "layout",
      "Не надсилати на пристрій; вивести прев’ю у stdout" },
    { "fit-page", no_argument, ARG_FIT_PAGE, '\0', NULL, "layout",
      "Масштабувати вміст, щоб вмістився на одну сторінку (без розбиття)" },
    { "png", no_argument, ARG_PNG, '\0', NULL, "layout", "При --preview вивести PNG замість SVG" },
    { "output", required_argument, ARG_OUTPUT, '\0', "PATH", "layout",
      "Тільки з --preview: зберегти у файл (без stdout)" },
    { "format", required_argument, ARG_FORMAT, '\0', "markdown", "layout",
      "Формат вхідного документа (доступно: markdown)" },
    { "family", required_argument, ARG_FONT_FAMILY_VALUE, '\0', "NAME|ID", "layout",
      "Родина або шрифт для поточного друку" },

    { "device-name", required_argument, ARG_DEVICE_NAME, '\0', "NAME", "device-settings",
      "Псевдонім пристрою з `device list`" },
    { "dx", required_argument, ARG_DX, '\0', "мм", "device-settings", "Зсув по X для jog" },
    { "dy", required_argument, ARG_DY, '\0', "мм", "device-settings", "Зсув по Y для jog" },

    { "list", no_argument, ARG_LIST, '\0', NULL, "font", "Перелічити доступні шрифти" },
    { "font-family", no_argument, ARG_FONT_FAMILIES, '\0', NULL, "font",
      "Список лише родин шрифтів" },

    { "show", no_argument, ARG_SHOW, '\0', NULL, "config", "Показати конфігурацію" },
    { "reset", no_argument, ARG_RESET, '\0', NULL, "config", "Скинути до типової" },
    { "set", required_argument, ARG_SET, '\0', "key=value[,..]", "config",
      "Задати ключі конфігурації" },
};

/* Підкоманда shape у CLI не підтримується. */

/**
 * @brief Повернути масив описів CLI-опцій разом із кількістю елементів.
 *
 * @param out_count Необов'язковий вихід: кількість елементів у масиві.
 * @return Вказівник на статичний масив описів.
 */
const cli_option_desc_t *argdefs_options (size_t *out_count) {
    if (out_count)
        *out_count = sizeof (k_option_descs) / sizeof (k_option_descs[0]);
    return k_option_descs;
}

/**
 * @brief Перелік підтримуваних підкоманд верхнього рівня.
 */
static const cli_command_desc_t k_commands[] = {
    { "print",
      "Плотинг із параметрами розкладки (для прев’ю використовуйте --preview, SVG/PNG у stdout)" },
    { "device", "Утиліти пристрою (profile, jog, pen, list)" },
    { "font", "Керування шрифтами (--list, псевдонім: fonts)" },
    { "config", "Показати або змінити типові налаштування" },
    { "version", "Показати версію" },
};

/**
 * @brief Надати інформацію про доступні підкоманди.
 *
 * @param out_count Необов'язковий вихід: кількість записів.
 * @return Статичний масив описів підкоманд.
 */
const cli_command_desc_t *argdefs_commands (size_t *out_count) {
    if (out_count)
        *out_count = sizeof (k_commands) / sizeof (k_commands[0]);
    return k_commands;
}

/* ---- Конфігураційні ключі (для секції help і валідації) ---- */
/**
 * @brief Опис ключів конфігурації, з якими працює CLI.
 *
 * Визначає тип значення, одиниці вимірювання та формат друку для help/config-модулів.
 */
static const cli_config_desc_t k_cfg_keys[] = {
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
    { "font_size", CFGK_DOUBLE, offsetof (config_t, font_size_pt), "pt", NULL,
      "Кегль шрифту за замовчуванням", "%.1f" },
    { "font_family", CFGK_ALIAS, 0, NULL, NULL, "Родина шрифтів за замовчуванням", NULL },
    { "speed", CFGK_DOUBLE, offsetof (config_t, speed_mm_s), "мм_с", NULL, "Швидкість", "%.1f" },
    { "accel", CFGK_DOUBLE, offsetof (config_t, accel_mm_s2), "мм_с2", NULL, "Прискорення",
      "%.1f" },
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

/**
 * @brief Отримати перелік доступних ключів конфігурації.
 *
 * @param out_count Необов'язковий вихід: кількість ключів.
 * @return Статичний масив із метаданими конфігураційних параметрів.
 */
const cli_config_desc_t *argdefs_config_keys (size_t *out_count) {
    if (out_count)
        *out_count = sizeof (k_cfg_keys) / sizeof (k_cfg_keys[0]);
    return k_cfg_keys;
}

/**
 * @brief Обробити коротку або довгу опцію, повернену `getopt_long`.
 *
 * @param arg     Код опції від `getopt_long` (включаючи штучні коди для long-опцій).
 * @param options Структура опцій, яку необхідно оновити (не NULL).
 */
void switch_options (int arg, options_t *options) {
    switch (arg) {
    case 'h':
        options->help = true;
        LOGD ("отримано прапорець допомоги");
        break;

    case 'v':
        options->version = true;
        LOGD ("отримано прапорець версії");
        break;

        /* long-опції обробляються вище у циклі; сюди вони не потрапляють */

    case '?':
        /* getopt уже вивів повідомлення про помилку */
        break;

    default:
        LOGW ("невідома опція: код=%d", arg);
        break;
    }
}

/**
 * @brief Розібрати аргумент `--margins` та оновити відповідні поля.
 *
 * @param value   Рядок аргументу у форматі `T[,R,B,L]` (міліметри).
 * @param options Структура опцій, у яку буде записано значення (не NULL).
 */
static void parse_margins_argument (const char *value, options_t *options) {
    if (!value || !options)
        return;
    double t = 0.0, r = 0.0, b = 0.0, l = 0.0;
    char buf[256];
    strncpy (buf, value, sizeof (buf) - 1);
    buf[sizeof (buf) - 1] = '\0';

    char *saveptr = NULL;
    char *token = strtok_r (buf, ",", &saveptr);
    int parts = 0;
    while (token && parts < 4) {
        char *end = NULL;
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
        ++parts;
        token = strtok_r (NULL, ",", &saveptr);
    }
    if (parts == 1) {
        r = b = l = t;
    }
    options->margin_top_mm = t;
    options->margin_right_mm = r;
    options->margin_bottom_mm = b;
    options->margin_left_mm = l;
}

/**
 * @brief Обробити опції, що стосуються розкладки сторінки.
 *
 * @param arg     Код опції (значення з `ARG_*`).
 * @param value   Значення опції, якщо вимагається (`NULL` для прапорців).
 * @param options Структура опцій для оновлення (не NULL).
 * @return `true`, якщо опція розпізнана та оброблена; `false` інакше.
 */
static bool handle_layout_option (int arg, const char *value, options_t *options) {
    switch (arg) {
    case ARG_PORTRAIT:
        options->orientation = ORIENT_PORTRAIT;
        LOGD ("орієнтація: портретна");
        return true;
    case ARG_LANDSCAPE:
        options->orientation = ORIENT_LANDSCAPE;
        LOGD ("орієнтація: альбомна");
        return true;
    case ARG_MARGINS:
        parse_margins_argument (value, options);
        return true;
    case ARG_WIDTH:
        if (value)
            options->paper_w_mm = atof (value);
        LOGD ("ширина=%.3f мм", options->paper_w_mm);
        return true;
    case ARG_HEIGHT:
        if (value)
            options->paper_h_mm = atof (value);
        LOGD ("висота=%.3f мм", options->paper_h_mm);
        return true;
    case ARG_FIT_PAGE:
        options->fit_page = true;
        LOGD ("масштаб: вміст у межах однієї сторінки");
        return true;
    case ARG_FORMAT:
        if (value && (strcmp (value, "markdown") == 0)) {
            options->input_format = INPUT_FORMAT_MARKDOWN;
            LOGD ("формат вводу: маркдаун");
        } else {
            LOGW ("Непідтримуваний параметр формату: %s (використовую текст)", value ? value : "");
            options->input_format = INPUT_FORMAT_TEXT;
        }
        return true;
    case ARG_FONT_FAMILY_VALUE:
        if (value) {
            string_copy (options->font_family, sizeof (options->font_family), value);
            LOGD ("родина/шрифт: %s", options->font_family);
        }
        return true;
    default:
        return false;
    }
}

/**
 * @brief Обробити опції, що впливають на режим друку/прев'ю.
 *
 * @param arg     Код опції (значення з `ARG_*`).
 * @param options Структура опцій для оновлення (не NULL).
 * @return `true`, якщо опція успішно оброблена.
 */
static bool handle_output_option (int arg, options_t *options) {
    switch (arg) {
    case ARG_PNG:
        options->preview_png = true;
        LOGD ("формат прев’ю: ПНГ");
        return true;
    case ARG_OUTPUT:
        if (options) {
            string_copy (options->output_path, sizeof (options->output_path), optarg ? optarg : "");
            LOGD ("прев’ю: файл виводу %s", options->output_path);
        }
        return true;
    case ARG_PREVIEW:
        options->preview = true;
        LOGD ("увімкнено прев’ю");
        return true;
    case ARG_DRY_RUN:
        options->dry_run = true;
        LOGD ("сухий запуск: без надсилання на пристрій");
        return true;
    case ARG_VERBOSE:
        options->verbose = true;
        LOGD ("детальний вивід");
        return true;
    default:
        return false;
    }
}

/* Вхідні текстові опції відсутні. */

/**
 * @brief Обробити опції підкоманди `font`/`fonts`.
 */
static bool handle_font_option (int arg, options_t *options) {
    if (!options)
        return false;
    if (options->cmd != CMD_FONTS)
        return false;
    switch (arg) {
    case ARG_LIST:
        options->fonts_list = true;
        LOGD ("шрифти: перелік");
        return true;
    case ARG_FONT_FAMILIES:
        options->fonts_list_families = true;
        LOGD ("шрифти: лише родини");
        return true;
    default:
        return false;
    }
}

/**
 * @brief Обробити опції, специфічні для підкоманди `device`.
 *
 * @param arg     Код опції (`ARG_DEVICE_NAME`, `ARG_DX`, `ARG_DY`, ...).
 * @param value   Значення опції або `NULL` для прапорців.
 * @param options Структура опцій для оновлення (не NULL).
 * @return `true`, якщо опція належить підкоманді `device` і оброблена.
 */
static bool handle_device_option (int arg, const char *value, options_t *options) {
    switch (arg) {
    case ARG_DEVICE_NAME:
        string_copy (options->remote_device, sizeof (options->remote_device), value);
        LOGD ("пристрій: псевдонім %s", options->remote_device);
        return true;
    case ARG_DX:
        options->jog_dx_mm = value ? atof (value) : 0.0;
        return true;
    case ARG_DY:
        options->jog_dy_mm = value ? atof (value) : 0.0;
        return true;
    case ARG_DEVICE_MODEL:
        string_copy (options->device_model, sizeof (options->device_model), value);
        LOGD ("модель пристрою: %s", options->device_model);
        return true;
    default:
        return false;
    }
}

/**
 * @brief Обробити опції, пов'язані з конфігурацією.
 *
 * @param arg     Код опції (`ARG_SHOW`, `ARG_RESET`, `ARG_SET`).
 * @param value   Значення аргументу (`key=value`), використовується для `--set`.
 * @param options Структура опцій для оновлення (не NULL).
 * @return `true`, якщо опція оброблена; `false` інакше.
 */
static bool handle_config_option (int arg, const char *value, options_t *options) {
    switch (arg) {
    case ARG_SHOW:
        options->config_action = CFG_SHOW;
        LOGD ("конфігурація: показ");
        return true;
    case ARG_RESET:
        options->config_action = CFG_RESET;
        LOGD ("конфігурація: скидання");
        return true;
    case ARG_SET:
        options->config_action = CFG_SET;
        string_copy (options->config_set_pairs, sizeof (options->config_set_pairs), value);
        LOGD ("конфігурація: встановлення %s", options->config_set_pairs);
        return true;
    default:
        return false;
    }
}

/**
 * @brief Встановити ім'я вхідного файлу з позиційного аргументу.
 *
 * Якщо після обробки опцій у `argv` лишився позиційний аргумент, він вважається
 * шляхом до вхідного файлу; інакше поле `file_name` очищується.
 *
 * @param argc         Кількість аргументів командного рядка.
 * @param argv         Масив аргументів командного рядка.
 * @param[in,out] options Структура опцій, поле `file_name` якої потрібно оновити.
 */
void get_file_name (int argc, char *argv[], options_t *options) {
    // Якщо є ще один аргумент — вважати його шляхом до вхідного файлу
    if (optind < argc) {
        string_copy (options->file_name, sizeof (options->file_name), argv[optind++]);
        LOGD ("вхідний файл: %s", options->file_name);
    } else {
        options->file_name[0] = '\0';
        LOGD ("вхідний файл не задано");
    }
}

/**
 * @brief Розібрати аргументи командного рядка у структуру `options_t`.
 *
 * Підтримує підкоманди (`print`, `device`, `fonts`, `config`, `version`) та
 * відповідні короткі/довгі опції. Функція є потокобезпечною завдяки внутрішньому mutex'у.
 *
 * @param argc    Кількість аргументів командного рядка.
 * @param argv    Масив аргументів командного рядка.
 * @param[out] options Структура, куди буде записано результат розбору (не NULL).
 */
void options_parser (int argc, char *argv[], options_t *options) {
    static pthread_mutex_t parser_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock (&parser_mutex);

    memset (options, 0, sizeof (*options));
    /* Позначаємо відсутні значення як NaN, щоб cmd самостійно вирішував дефолти. */
    options->paper_w_mm = NAN;
    options->paper_h_mm = NAN;
    options->fit_page = true; /* default: single-page fit */
    options->margin_top_mm = NAN;
    options->margin_right_mm = NAN;
    options->margin_bottom_mm = NAN;
    options->margin_left_mm = NAN;
    options->font_size_pt = NAN;

    int arg; /* Поточна опція */

// Скинути стан getopt для повторних викликів (тести викликають парсер багаторазово)
#ifdef __APPLE__
    extern int optreset;
    optreset = 1;
#endif
    optind = 1;

    // Розібрати підкоманду, якщо присутня (argv[1])
    if (argc >= 2 && argv[1][0] != '-') {
        cmd_t parsed = parse_command_name (argv[1]);
        if (parsed != CMD_NONE) {
            options->cmd = parsed;
            LOGD ("виявлено підкоманду: %d", options->cmd);
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

        if (arg == 0) {
            options->no_colors = true;
            continue;
        }

        if (handle_layout_option (arg, optarg, options))
            continue;
        if (handle_output_option (arg, options))
            continue;
        /* Опція --text не підтримується */
        if (handle_font_option (arg, options))
            continue;
        if (handle_device_option (arg, optarg, options))
            continue;
        if (handle_config_option (arg, optarg, options))
            continue;
        /* shape-опції не підтримуються у CLI */

        switch_options (arg, options);
    }

    /* Якщо підкоманду ще не визначено (наприклад, коли глобальні опції були перед командою),
     * спробувати розпізнати її у першому позиційному токені після getopt. */
    if (options->cmd == CMD_NONE && optind < argc) {
        cmd_t parsed_tail = parse_command_name (argv[optind]);
        if (parsed_tail != CMD_NONE) {
            options->cmd = parsed_tail;
            LOGD ("виявлено підкоманду після опцій: %d", options->cmd);
            ++optind; /* зсунутись повз назву підкоманди */
        }
    }

    /* Handle device positional action (e.g., up, down, jog). On some libc
     * (e.g., macOS), getopt_long stops at the first non-option token, so make a
     * robust pass to capture positional action and any dx/dy that follow it. */
    if (options->cmd == CMD_DEVICE) {
        device_parse_result_t parsed
            = parse_device_tokens (argc, argv, optind, options->device_action);
        if (parsed.action_set) {
            options->device_action = parsed.action;
            LOGD (
                "дія пристрою: тип=%d pen=%d мотор=%d", (int)options->device_action.kind,
                (int)options->device_action.pen, (int)options->device_action.motor);
        }
        if (parsed.dx_set) {
            options->jog_dx_mm = parsed.jog_dx_mm;
            LOGD ("jog dx=%.3f мм", options->jog_dx_mm);
        }
        if (parsed.dy_set) {
            options->jog_dy_mm = parsed.jog_dy_mm;
            LOGD ("jog dy=%.3f мм", options->jog_dy_mm);
        }
    }

    // Отримати шлях до файлу; тільки для команд, що очікують файл
    if (options->cmd == CMD_PRINT) {
        get_file_name (argc, argv, options);
    }

    /* shape-підкоманда вилучена */

    pthread_mutex_unlock (&parser_mutex);
}
