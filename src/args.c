/**
 * @file args.c
 * @brief Реалізація розбору аргументів CLI.
 * @ingroup args
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
 * @brief Створює значення дії пристрою без вибраної дії.
 * @return Обʼєкт дії з kind=DEVICE_ACTION_NONE.
 */
static device_action_t args_device_action_make_none (void) {
    device_action_t action
        = { .kind = DEVICE_ACTION_NONE, .pen = DEVICE_PEN_NONE, .motor = DEVICE_MOTOR_NONE };
    return action;
}

/**
 * @brief Створює просту дію пристрою за типом.
 * @param kind Тип дії (list, abort, home, ...).
 * @return Обʼєкт дії із заданим типом.
 */
static device_action_t args_device_action_make_simple (device_action_kind_t kind) {
    device_action_t action = args_device_action_make_none ();
    action.kind = kind;
    return action;
}

/**
 * @brief Створює дію для пера (up/down/toggle).
 * @param pen Вказана піддія пера.
 * @return Обʼєкт дії типу DEVICE_ACTION_PEN.
 */
static device_action_t args_device_action_make_pen (device_pen_action_t pen) {
    device_action_t action = args_device_action_make_simple (DEVICE_ACTION_PEN);
    action.pen = pen;
    return action;
}

/**
 * @brief Створює дію для моторів (on/off).
 * @param motor Бажаний стан моторів.
 * @return Обʼєкт дії типу DEVICE_ACTION_MOTORS.
 */
static device_action_t args_device_action_make_motor (device_motor_action_t motor) {
    device_action_t action = args_device_action_make_simple (DEVICE_ACTION_MOTORS);
    action.motor = motor;
    return action;
}

/**
 * @brief Перевіряє, чи встановлено якусь дію пристрою.
 * @param action Вказівник на дію.
 * @return true — дію встановлено, false — відсутня.
 */
static bool args_device_action_is_set (const device_action_t *action) {
    return action && action->kind != DEVICE_ACTION_NONE;
}

/**
 * @brief Розпізнає підкоманду за її назвою.
 * @param name Токен назви підкоманди.
 * @return Код підкоманди або CMD_NONE.
 */
static cmd_t args_parse_command_name (const char *name) {
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
 * @brief Перетворює рядок-токен у дію пристрою.
 * @param token Рядок (напр., "pen", "motors-on", "status").
 * @return Визначена дія або відсутня.
 */
static device_action_t args_device_action_from_token (const char *token) {
    if (!token)
        return args_device_action_make_none ();

    if (strcmp (token, "list") == 0)
        return args_device_action_make_simple (DEVICE_ACTION_LIST);
    if (strcmp (token, "up") == 0 || strcmp (token, "pen-up") == 0)
        return args_device_action_make_pen (DEVICE_PEN_UP);
    if (strcmp (token, "down") == 0 || strcmp (token, "pen-down") == 0)
        return args_device_action_make_pen (DEVICE_PEN_DOWN);
    if (strcmp (token, "toggle") == 0 || strcmp (token, "pen-toggle") == 0)
        return args_device_action_make_pen (DEVICE_PEN_TOGGLE);
    if (strcmp (token, "motors-on") == 0)
        return args_device_action_make_motor (DEVICE_MOTOR_ON);
    if (strcmp (token, "motors-off") == 0)
        return args_device_action_make_motor (DEVICE_MOTOR_OFF);
    if (strcmp (token, "abort") == 0)
        return args_device_action_make_simple (DEVICE_ACTION_ABORT);
    if (strcmp (token, "home") == 0)
        return args_device_action_make_simple (DEVICE_ACTION_HOME);
    if (strcmp (token, "jog") == 0)
        return args_device_action_make_simple (DEVICE_ACTION_JOG);
    if (strcmp (token, "version") == 0)
        return args_device_action_make_simple (DEVICE_ACTION_VERSION);
    if (strcmp (token, "status") == 0)
        return args_device_action_make_simple (DEVICE_ACTION_STATUS);
    if (strcmp (token, "position") == 0)
        return args_device_action_make_simple (DEVICE_ACTION_POSITION);
    if (strcmp (token, "reset") == 0)
        return args_device_action_make_simple (DEVICE_ACTION_RESET);
    if (strcmp (token, "reboot") == 0)
        return args_device_action_make_simple (DEVICE_ACTION_REBOOT);
    if (strcmp (token, "profile") == 0)
        return args_device_action_make_simple (DEVICE_ACTION_PROFILE);

    return args_device_action_make_none ();
}

typedef struct {
    device_action_t action;
    bool action_set;
    double jog_dx_mm;
    bool dx_set;
    double jog_dy_mm;
    bool dy_set;
} device_parse_result_t;

static device_parse_result_t
/**
 * @brief Розбір токенів підкоманди device після основних опцій.
 * @param argc Загальна кількість аргументів.
 * @param argv Масив аргументів.
 * @param current_optind Поточний індекс getopt після обробки опцій.
 * @param initial_action Початкова дія (якщо задано прапорцями).
 * @return Результат з вибраною дією та параметрами jog.
 */
args_parse_device_tokens (int argc, char *argv[], int current_optind, device_action_t initial_action) {
    device_parse_result_t result = { .action = initial_action,
                                     .action_set = args_device_action_is_set (&initial_action),
                                     .jog_dx_mm = 0.0,
                                     .dx_set = false,
                                     .jog_dy_mm = 0.0,
                                     .dy_set = false };

    for (int i = current_optind; i < argc; ++i) {
        const char *token = argv[i];
        if (!token || !*token)
            continue;

        if (strcmp (token, "motors") == 0) {
            const char *next = (i + 1 < argc) ? argv[i + 1] : NULL;
            if (next && next[0] != '-') {
                if (strcmp (next, "on") == 0) {
                    result.action = args_device_action_make_motor (DEVICE_MOTOR_ON);
                    result.action_set = true;
                    ++i;
                    continue;
                }
                if (strcmp (next, "off") == 0) {
                    result.action = args_device_action_make_motor (DEVICE_MOTOR_OFF);
                    result.action_set = true;
                    ++i;
                    continue;
                }
            }
        }

        if (token[0] == '-') {

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

        if (!result.action_set) {
            device_action_t candidate = args_device_action_from_token (token);
            if (args_device_action_is_set (&candidate)) {
                result.action = candidate;
                result.action_set = true;
            }
        }
    }

    if (!result.action_set)
        result.action = args_device_action_make_none ();

    return result;
}

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
    { "motion-profile", required_argument, 0, 25 },
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
 * @brief Повертає масив довгих опцій для getopt_long.
 * @return Вказівник на статичний масив опцій.
 */
const struct option *argdefs_long_options (void) { return k_long_options; }

static const cli_option_desc_t k_option_descs_global[] = {
    { "help", no_argument, 'h', 'h', NULL, "global", "Показати довідку" },
    { "version", no_argument, 'v', 'v', NULL, "global", "Показати версію" },
    { "no-colors", no_argument, ARG_NO_COLORS, '\0', NULL, "global", "Вимкнути ANSI-кольори" },
    { "verbose", no_argument, ARG_VERBOSE, '\0', NULL, "global", "Розгорнутий вивід" },
    { "dry-run", no_argument, ARG_DRY_RUN, '\0', NULL, "global",
      "Не надсилати команди на пристрій" },
};

static const cli_option_desc_t k_option_descs_print[] = {
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
    { "motion-profile", required_argument, 25, '\0', "precise|balanced|fast", "layout",
      "Профіль руху (швидкість/прискорення)" },
};

static const cli_option_desc_t k_option_descs_device[] = {
    { "device-name", required_argument, ARG_DEVICE_NAME, '\0', "NAME", "device-settings",
      "Псевдонім пристрою з `device list`" },
    { "dx", required_argument, ARG_DX, '\0', "мм", "device-settings", "Зсув по X для jog" },
    { "dy", required_argument, ARG_DY, '\0', "мм", "device-settings", "Зсув по Y для jog" },
};

static const cli_option_desc_t k_option_descs_fonts[] = {
    { "list", no_argument, ARG_LIST, '\0', NULL, "font", "Перелічити доступні шрифти" },
    { "font-family", no_argument, ARG_FONT_FAMILIES, '\0', NULL, "font",
      "Список лише родин шрифтів" },
};

static const cli_option_desc_t k_option_descs_config[] = {
    { "show", no_argument, ARG_SHOW, '\0', NULL, "config", "Показати конфігурацію" },
    { "reset", no_argument, ARG_RESET, '\0', NULL, "config", "Скинути до типової" },
    { "set", required_argument, ARG_SET, '\0', "key=value[,..]", "config",
      "Задати ключі конфігурації" },
    { "device", required_argument, ARG_DEVICE_MODEL, '\0', "name", "config",
      "Обрати профіль пристрою (напр., minikit2)" },
};

#define ARRAY_COUNT(arr) (sizeof (arr) / sizeof ((arr)[0]))

static cli_option_desc_t g_option_descs
    [ARRAY_COUNT (k_option_descs_global) + ARRAY_COUNT (k_option_descs_print)
     + ARRAY_COUNT (k_option_descs_device) + ARRAY_COUNT (k_option_descs_fonts)
     + ARRAY_COUNT (k_option_descs_config)]
    = { 0 };

static size_t g_option_descs_count = 0;

static void args_option_descs_init (void) {
    if (g_option_descs_count > 0)
        return;

    size_t idx = 0;

#define COPY_DESC_BLOCK(block)                                                                     \
    memcpy (&g_option_descs[idx], block, sizeof (block));                                          \
    idx += ARRAY_COUNT (block)

    COPY_DESC_BLOCK (k_option_descs_global);
    COPY_DESC_BLOCK (k_option_descs_print);
    COPY_DESC_BLOCK (k_option_descs_device);
    COPY_DESC_BLOCK (k_option_descs_fonts);
    COPY_DESC_BLOCK (k_option_descs_config);

#undef COPY_DESC_BLOCK

    g_option_descs_count = idx;
}

/**
 * @brief Повертає внутрішній опис опцій для побудови хелпу.
 * @param out_count [out] Кількість елементів у масиві.
 * @return Вказівник на статичний масив описів.
 */
const cli_option_desc_t *args_argdefs_options (size_t *out_count) {
    args_option_descs_init ();
    if (out_count)
        *out_count = g_option_descs_count;
    return g_option_descs;
}

static const cli_command_desc_t k_commands[] = {
    { "print",
      "Плотинг із параметрами розкладки (для прев’ю використовуйте --preview, SVG/PNG у stdout)" },
    { "device", "Утиліти пристрою (profile, jog, pen, list)" },
    { "font", "Керування шрифтами (--list, псевдонім: fonts)" },
    { "config", "Показати або змінити типові налаштування" },
    { "version", "Показати версію" },
};

/**
 * @brief Повертає список підтримуваних підкоманд.
 * @param out_count [out] Кількість елементів.
 */
const cli_command_desc_t *args_argdefs_commands (size_t *out_count) {
    if (out_count)
        *out_count = sizeof (k_commands) / sizeof (k_commands[0]);
    return k_commands;
}

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
 * @brief Повертає опис підтримуваних ключів конфігурації (для help/config).
 * @param out_count [out] Кількість елементів.
 */
const cli_config_desc_t *args_argdefs_config_keys (size_t *out_count) {
    if (out_count)
        *out_count = sizeof (k_cfg_keys) / sizeof (k_cfg_keys[0]);
    return k_cfg_keys;
}

/**
 * @brief Обробляє глобальні короткі опції (-h, -v) та перемикає прапорці.
 * @param arg Код опції з getopt_long.
 * @param options Структура для заповнення.
 */
void args_switch_options (int arg, options_t *options) {
    switch (arg) {
    case 'h':
        options->help = true;
        LOGD ("отримано прапорець допомоги");
        break;

    case 'v':
        options->version = true;
        LOGD ("отримано прапорець версії");
        break;

    case '?':

        break;

    default:
        LOGW ("невідома опція: код=%d", arg);
        break;
    }
}

/**
 * @brief Розбирає аргумент полів у форматі T[,R,B,L] (мм).
 * @param value Рядок значення.
 * @param options Структура CLI для заповнення.
 */
static void args_parse_margins_argument (const char *value, options_t *options) {
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
    options->print.margin_top_mm = t;
    options->print.margin_right_mm = r;
    options->print.margin_bottom_mm = b;
    options->print.margin_left_mm = l;
}

/**
 * @brief Обробляє опції розкладки/сторінки.
 * @param arg Код опції.
 * @param value Значення (якщо є).
 * @param options [in,out] Параметри CLI.
 * @return true — опцію оброблено, false — ні.
 */
static bool args_handle_layout_option (int arg, const char *value, options_t *options) {
    switch (arg) {
    case ARG_PORTRAIT:
        options->print.orientation = ORIENT_PORTRAIT;
        LOGD ("орієнтація: портретна");
        return true;
    case ARG_LANDSCAPE:
        options->print.orientation = ORIENT_LANDSCAPE;
        LOGD ("орієнтація: альбомна");
        return true;
    case ARG_MARGINS:
        args_parse_margins_argument (value, options);
        return true;
    case ARG_WIDTH:
        if (value)
            options->print.paper_w_mm = atof (value);
        LOGD ("ширина=%.3f мм", options->print.paper_w_mm);
        return true;
    case ARG_HEIGHT:
        if (value)
            options->print.paper_h_mm = atof (value);
        LOGD ("висота=%.3f мм", options->print.paper_h_mm);
        return true;
    case ARG_FIT_PAGE:
        options->print.fit_page = true;
        LOGD ("масштаб: вміст у межах однієї сторінки");
        return true;
    case ARG_FORMAT:
        if (value && (strcmp (value, "markdown") == 0)) {
            options->print.input_format = INPUT_FORMAT_MARKDOWN;
            LOGD ("формат вводу: маркдаун");
        } else {
            LOGW ("Непідтримуваний параметр формату: %s (використовую текст)", value ? value : "");
            options->print.input_format = INPUT_FORMAT_TEXT;
        }
        return true;
    case ARG_FONT_FAMILY_VALUE:
        if (value) {
            str_string_copy (
                options->print.font_family, sizeof (options->print.font_family), value);
            LOGD ("родина/шрифт: %s", options->print.font_family);
        }
        return true;
    case 25: {
        if (value == NULL) {
            options->print.motion_profile = MOTION_PROFILE_BALANCED;
            return true;
        }
        if (strcmp (value, "precise") == 0) {
            options->print.motion_profile = MOTION_PROFILE_PRECISE;
            LOGD ("профіль руху: precise");
        } else if (strcmp (value, "fast") == 0) {
            options->print.motion_profile = MOTION_PROFILE_FAST;
            LOGD ("профіль руху: fast");
        } else {
            options->print.motion_profile = MOTION_PROFILE_BALANCED;
            LOGD ("профіль руху: balanced");
        }
        return true;
    }
    default:
        return false;
    }
}

/**
 * @brief Обробляє опції виводу/превʼю.
 * @param arg Код опції.
 * @param options [in,out] Параметри CLI.
 * @return true — оброблено, false — ні.
 */
static bool args_handle_output_option (int arg, options_t *options) {
    switch (arg) {
    case ARG_PNG:
        options->print.preview_png = true;
        LOGD ("формат прев’ю: ПНГ");
        return true;
    case ARG_OUTPUT:
        if (options) {
            str_string_copy (
                options->print.output_path, sizeof (options->print.output_path),
                optarg ? optarg : "");
            LOGD ("прев’ю: файл виводу %s", options->print.output_path);
        }
        return true;
    case ARG_PREVIEW:
        options->print.preview = true;
        LOGD ("увімкнено прев’ю");
        return true;
    case ARG_DRY_RUN:
        options->print.dry_run = true;
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

/**
 * @brief Обробляє опції вибору шрифту та форматування вводу.
 * @param arg Код опції.
 * @param options [in,out] Параметри CLI.
 * @return true — оброблено, false — ні.
 */
static bool args_handle_font_option (int arg, options_t *options) {
    if (!options)
        return false;
    if (options->cmd != CMD_FONTS)
        return false;
    switch (arg) {
    case ARG_LIST:
        options->fonts.list = true;
        LOGD ("шрифти: перелік");
        return true;
    case ARG_FONT_FAMILIES:
        options->fonts.list_families = true;
        LOGD ("шрифти: лише родини");
        return true;
    default:
        return false;
    }
}

/**
 * @brief Обробляє опції, повʼязані з пристроєм (device-name, dx, dy).
 * @param arg Код опції.
 * @param value Значення (якщо є).
 * @param options [in,out] Параметри CLI.
 * @return true — оброблено, false — ні.
 */
static bool args_handle_device_option (int arg, const char *value, options_t *options) {
    switch (arg) {
    case ARG_DEVICE_NAME:
        str_string_copy (
            options->device.remote_device, sizeof (options->device.remote_device), value);
        LOGD ("пристрій: псевдонім %s", options->device.remote_device);
        return true;
    case ARG_DX:
        options->device.jog_dx_mm = value ? atof (value) : 0.0;
        return true;
    case ARG_DY:
        options->device.jog_dy_mm = value ? atof (value) : 0.0;
        return true;
    case ARG_DEVICE_MODEL:
        str_string_copy (
            options->device.device_model, sizeof (options->device.device_model), value);
        str_string_copy (options->print.device_model, sizeof (options->print.device_model), value);
        LOGD ("модель пристрою: %s", options->device.device_model);
        return true;
    default:
        return false;
    }
}

/**
 * @brief Обробляє опції підкоманди config (--show/--reset/--set key=val,...).
 * @param arg Код опції.
 * @param value Значення (для --set).
 * @param options [in,out] Параметри CLI.
 * @return true — оброблено, false — ні.
 */
static bool args_handle_config_option (int arg, const char *value, options_t *options) {
    switch (arg) {
    case ARG_SHOW:
        options->config.action = CFG_SHOW;
        LOGD ("конфігурація: показ");
        return true;
    case ARG_RESET:
        options->config.action = CFG_RESET;
        LOGD ("конфігурація: скидання");
        return true;
    case ARG_SET:
        options->config.action = CFG_SET;
        str_string_copy (options->config.set_pairs, sizeof (options->config.set_pairs), value);
        LOGD ("конфігурація: встановлення %s", options->config.set_pairs);
        return true;
    default:
        return false;
    }
}

/**
 * @brief Зчитує позиційний аргумент — імʼя вхідного файлу, якщо присутній.
 * @param argc Кількість аргументів.
 * @param argv Масив аргументів.
 * @param options [out] Для запису `file_name` або порожньо.
 */
void args_get_file_name (int argc, char *argv[], options_t *options) {

    if (optind < argc) {
        str_string_copy (
            options->print.file_name, sizeof (options->print.file_name), argv[optind++]);
        LOGD ("вхідний файл: %s", options->print.file_name);
    } else {
        options->print.file_name[0] = '\0';
        LOGD ("вхідний файл не задано");
    }
}

/**
 * @brief Повний розбір аргументів командного рядка у потокобезпечному блоці.
 * @param argc Кількість аргументів.
 * @param argv Масив аргументів.
 * @param options [out] Структура результатів розбору.
 */
void args_options_parser (int argc, char *argv[], options_t *options) {
    static pthread_mutex_t parser_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock (&parser_mutex);

    memset (options, 0, sizeof (*options));

    options->print.paper_w_mm = NAN;
    options->print.paper_h_mm = NAN;
    options->print.fit_page = true;
    options->print.margin_top_mm = NAN;
    options->print.margin_right_mm = NAN;
    options->print.margin_bottom_mm = NAN;
    options->print.margin_left_mm = NAN;
    options->print.font_size_pt = NAN;
    options->print.motion_profile = MOTION_PROFILE_BALANCED;

    int arg;

#ifdef __APPLE__
    extern int optreset;
    optreset = 1;
#endif
    optind = 1;

    if (argc >= 2 && argv[1][0] != '-') {
        cmd_t parsed = args_parse_command_name (argv[1]);
        if (parsed != CMD_NONE) {
            options->cmd = parsed;
            LOGD ("виявлено підкоманду: %d", options->cmd);
            argv++;
            argc--;
        }
    }

    const struct option *long_options = argdefs_long_options ();

    while (true) {

        int option_index = 0;
        arg = getopt_long (argc, argv, "hv", long_options, &option_index);

        if (arg == -1)
            break;

        if (arg == 0) {
            options->no_colors = true;
            continue;
        }

        if (args_handle_layout_option (arg, optarg, options))
            continue;
        if (args_handle_output_option (arg, options))
            continue;

        if (args_handle_font_option (arg, options))
            continue;
        if (args_handle_device_option (arg, optarg, options))
            continue;
        if (args_handle_config_option (arg, optarg, options))
            continue;

        args_switch_options (arg, options);
    }

    if (options->cmd == CMD_NONE && optind < argc) {
        cmd_t parsed_tail = args_parse_command_name (argv[optind]);
        if (parsed_tail != CMD_NONE) {
            options->cmd = parsed_tail;
            LOGD ("виявлено підкоманду після опцій: %d", options->cmd);
            ++optind;
        }
    }

    if (options->cmd == CMD_DEVICE) {
        device_parse_result_t parsed
            = args_parse_device_tokens (argc, argv, optind, options->device.action);
        if (parsed.action_set) {
            options->device.action = parsed.action;
            LOGD (
                "дія пристрою: тип=%d pen=%d мотор=%d", (int)options->device.action.kind,
                (int)options->device.action.pen, (int)options->device.action.motor);
        }
        if (parsed.dx_set) {
            options->device.jog_dx_mm = parsed.jog_dx_mm;
            LOGD ("jog dx=%.3f мм", options->device.jog_dx_mm);
        }
        if (parsed.dy_set) {
            options->device.jog_dy_mm = parsed.jog_dy_mm;
            LOGD ("jog dy=%.3f мм", options->device.jog_dy_mm);
        }
    }

    if (options->cmd == CMD_PRINT) {
        args_get_file_name (argc, argv, options);
    }

    pthread_mutex_unlock (&parser_mutex);
}
