/**
 * @file args.h
 * @brief Аргументи командного рядка та розбір опцій.
 *
 * Визначає типи CLI, видимі користувачу (підкоманди, дії пристрою/конфігурації),
 * а також структуру options, яку використовує вся програма. Надає точку входу
 * для парсера, що заповнює options_t за даними argc/argv.
 *
 * Також містить централізовані машинно-читані описи опцій і підкоманд,
 * які раніше були у argdefs.h/argdefs.c. Вони використовуються як парсером,
 * так і модулем довідки для автогенерації розділів Usage/Options.
 */
#ifndef ARGS_H
#define ARGS_H

#include <getopt.h>
#include <stdbool.h>
#include <stddef.h>

/// Максимальний розмір імені вхідного файлу (разом із NUL).
#define FILE_NAME_SIZE 512

/// Підкоманди, які підтримує CLI.
typedef enum {
    CMD_NONE = 0,
    CMD_PRINT,
    CMD_DEVICE,
    CMD_FONTS,
    CMD_CONFIG,
    CMD_VERSION,
    CMD_SYSINFO
} cmd_t;

/// Опція орієнтації сторінки.
typedef enum { ORIENT_AUTO = 0, ORIENT_PORTRAIT, ORIENT_LANDSCAPE } orientation_t;

/**
 * @enum device_action_kind_t
 * @brief Категорії верхнього рівня для підкоманди `device`.
 *
 * Визначає базову гілку логіки, яку буде виконано у cmd_device_execute().
 */
typedef enum {
    DEVICE_ACTION_NONE = 0,
    DEVICE_ACTION_LIST,
    DEVICE_ACTION_PEN,
    DEVICE_ACTION_MOTORS,
    DEVICE_ACTION_HOME,
    DEVICE_ACTION_JOG,
    DEVICE_ACTION_VERSION,
    DEVICE_ACTION_STATUS,
    DEVICE_ACTION_POSITION,
    DEVICE_ACTION_RESET,
    DEVICE_ACTION_REBOOT
} device_action_kind_t;

/**
 * @enum device_pen_action_t
 * @brief Деталізація дій для категорії DEVICE_ACTION_PEN.
 */
typedef enum {
    DEVICE_PEN_NONE = 0,
    DEVICE_PEN_UP,
    DEVICE_PEN_DOWN,
    DEVICE_PEN_TOGGLE
} device_pen_action_t;

/**
 * @enum device_motor_action_t
 * @brief Деталізація дій для категорії DEVICE_ACTION_MOTORS.
 */
typedef enum { DEVICE_MOTOR_NONE = 0, DEVICE_MOTOR_ON, DEVICE_MOTOR_OFF } device_motor_action_t;

/**
 * @struct device_action_t
 * @brief Композитна дія пристрою з деталізацією для підкатегорій.
 *
 * Поле kind визначає гілку виконання, а pen/motor уточнюють поведінку для
 * відповідних категорій. Для решти категорій ці уточнення ігноруються.
 */
typedef struct {
    device_action_kind_t kind;   /**< базова категорія дії */
    device_pen_action_t pen;     /**< уточнення для дій пера */
    device_motor_action_t motor; /**< уточнення для дій моторів */
} device_action_t;

/// Дія конфігурації для підкоманди `config`.
typedef enum { CFG_NONE = 0, CFG_SHOW, CFG_RESET, CFG_SET } config_action_t;

/** Коди опцій для getopt_long (узгоджено з switch у args.c). */
typedef enum arg_code {
    ARG_NO_COLORS = 0,
    ARG_PORTRAIT = 1,
    ARG_LANDSCAPE = 2,
    ARG_MARGINS = 4,
    ARG_PAPER_W = 5,
    ARG_PAPER_H = 6,
    ARG_PNG = 8,
    ARG_DRY_RUN = 9,
    ARG_VERBOSE = 10,
    ARG_LIST = 11,
    ARG_PORT = 12,
    ARG_DX = 13,
    ARG_DY = 14,
    ARG_FAMILY = 15,
    ARG_SHOW = 16,
    ARG_RESET = 17,
    ARG_SET = 18,
    ARG_DEVICE_MODEL = 19,
    ARG_PREVIEW = 20
} arg_code_t;

/** Опис однієї опції CLI. */
typedef struct cli_option_desc {
    const char *long_name;       /**< довге ім'я без префіксу -- */
    int has_arg;                 /**< no_argument | required_argument */
    int code;                    /**< значення val для getopt_long */
    char short_name;             /**< коротка літера або '\0' */
    const char *arg_placeholder; /**< плейсхолдер аргументу, напр. "мм" або "PATH" */
    const char *group;           /**< група: global|layout|device|config */
    const char *description;     /**< опис українською */
} cli_option_desc_t;

/** Опис підкоманди для розділу Usage. */
typedef struct cli_command_desc {
    const char *name;        /**< ім'я підкоманди */
    const char *description; /**< опис українською */
} cli_command_desc_t;

/** Тип значення конфігураційного ключа. */
typedef enum {
    CFGK_INT = 0,    /**< ціле число */
    CFGK_DOUBLE = 1, /**< дійсне число */
    CFGK_ENUM = 2,   /**< перерахування (рядок зі списком значень) */
    CFGK_ALIAS = 3   /**< псевдонім/агрегація, без прямого поля у config_t */
} cfg_valtype_t;

/**
 * @brief Опис конфігураційного ключа для --config --set.
 *
 * Використовується для автогенерації довідки та валідації введення у майбутньому.
 * Для типів INT/DOUBLE/ENUM поле offset має вказувати на відповідне поле у config_t
 * (використовуйте offsetof). Для типу ENUM перечисліть можливі значення у enum_values,
 * розділені символом '|'. Для CFGK_ALIAS offset і enum_values ігноруються.
 */
typedef struct cli_config_desc {
    const char *key;         /**< ім'я ключа (ліворуч від '=') */
    cfg_valtype_t type;      /**< тип значення ключа */
    size_t offset;           /**< offsetof(config_t, field) для INT/DOUBLE/ENUM */
    const char *unit;        /**< одиниця виміру (напр., "мм", "%", "мс", або NULL) */
    const char *enum_values; /**< варіанти для ENUM, розділені '|', або NULL */
    const char *description; /**< короткий опис українською */
    const char *fmt;         /**< формат для друку типового значення (напр., "%.1f" або "%d") */
} cli_config_desc_t;

/**
 * @brief Розібрані опції командного рядка.
 *
 * Цю структуру заповнює options_parser(), її читає диспетчер CLI та реалізації
 * підкоманд. Поля згруповані за призначенням.
 */
struct options {
    // Глобальні прапорці
    bool help;                      /**< запитано --help */
    bool version;                   /**< запитано --version */
    bool use_colors;                /**< вимкнути через --no-colors */
    cmd_t cmd;                      /**< розібрана підкоманда (або CMD_NONE) */
    char file_name[FILE_NAME_SIZE]; /**< шлях до вхідного файлу або "-" для stdin */

    // Прапорці розкладки
    orientation_t orientation; /**< орієнтація сторінки (типово портрет) */
    double margin_top_mm;      /**< верхнє поле в мм */
    double margin_right_mm;    /**< праве поле в мм */
    double margin_bottom_mm;   /**< нижнє поле в мм */
    double margin_left_mm;     /**< ліве поле в мм */
    double paper_w_mm;         /**< ширина паперу в мм (0 = не задано) */
    double paper_h_mm;         /**< висота паперу в мм (0 = не задано) */

    // Прапорці виводу та режимів
    bool preview;     /**< --preview: згенерувати прев’ю у stdout (SVG або PNG) */
    bool preview_png; /**< при --preview обрати PNG замість SVG */
    bool dry_run;
    bool verbose;
    // Типографіка
    char font_family[128]; /**< напр., "Serif Bold" або id "hershey_serif_bold" */
    // Вибір моделі пристрою (напр., "minikit2"). Порожньо = значення за замовчуванням.
    char device_model[32];

    // Прапорці пристрою (підкоманда device)
    device_action_t device_action; /**< обрана дія пристрою */
    char device_port[128];
    double jog_dx_mm;
    double jog_dy_mm;

    // Прапорці конфігурації
    config_action_t config_action;
    char config_set_pairs[512]; /**< пари key=value через кому */
};

/** Експортує options як глобальний псевдонім типу. */
typedef struct options options_t;

/**
 * Розібрати аргументи командного рядка у options_t.
 *
 * Підтримує підкоманди (print, device, fonts, config, version, sysinfo) та їхні опції.
 * Парсер реентерабельний: скидає стан getopt, щоб його можна було викликати
 * багаторазово у тестах.
 *
 * @param argc    Кількість аргументів з main().
 * @param argv    Вектор аргументів з main().
 * @param options Структура для заповнення (не NULL).
 */
void options_parser (int argc, char *argv[], options_t *options);

/**
 * Повернути статичний масив struct option для getopt_long (із термінатором).
 * Масив дійсний протягом усього часу виконання.
 */
const struct option *argdefs_long_options (void);

/** Отримати масив описів опцій та його розмір. */
const cli_option_desc_t *argdefs_options (size_t *out_count);

/** Отримати масив описів підкоманд та його розмір. */
const cli_command_desc_t *argdefs_commands (size_t *out_count);

/** Отримати масив описів конфігураційних ключів та його розмір. */
const cli_config_desc_t *argdefs_config_keys (size_t *out_count);

#endif // ARGS_H
