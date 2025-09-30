/**
 * @file args.h
 * @brief Опис структур та функцій розбору аргументів CLI.
 * @defgroup args Аргументи
 * @ingroup cli
 */
#ifndef ARGS_H
#define ARGS_H

#include <getopt.h>
#include <stdbool.h>
#include <stddef.h>

#include "config.h"

#define FILE_NAME_SIZE 512

/**
 * @brief Підтримувані верхньорівневі підкоманди.
 */
typedef enum { CMD_NONE = 0, CMD_PRINT, CMD_DEVICE, CMD_FONTS, CMD_CONFIG, CMD_VERSION } cmd_t;

/**
 * @brief Перелік дій для підкоманди `device`.
 */
typedef enum {
    DEVICE_ACTION_NONE = 0,
    DEVICE_ACTION_LIST,
    DEVICE_ACTION_PEN,
    DEVICE_ACTION_MOTORS,
    DEVICE_ACTION_ABORT,
    DEVICE_ACTION_HOME,
    DEVICE_ACTION_JOG,
    DEVICE_ACTION_VERSION,
    DEVICE_ACTION_STATUS,
    DEVICE_ACTION_POSITION,
    DEVICE_ACTION_RESET,
    DEVICE_ACTION_REBOOT,
    DEVICE_ACTION_PROFILE
} device_action_kind_t;

/**
 * @brief Дії з пером пристрою.
 */
typedef enum {
    DEVICE_PEN_NONE = 0,
    DEVICE_PEN_UP,
    DEVICE_PEN_DOWN,
    DEVICE_PEN_TOGGLE
} device_pen_action_t;

/**
 * @brief Керування моторами пристрою.
 */
typedef enum { DEVICE_MOTOR_NONE = 0, DEVICE_MOTOR_ON, DEVICE_MOTOR_OFF } device_motor_action_t;

/**
 * @brief Параметри дії над пристроєм AxiDraw.
 */
typedef struct {
    device_action_kind_t kind;
    device_pen_action_t pen;
    device_motor_action_t motor;
} device_action_t;

/**
 * @brief Дії підкоманди `config`.
 */
typedef enum { CFG_NONE = 0, CFG_SHOW, CFG_RESET, CFG_SET } config_action_t;

/**
 * @brief Коди довгих/коротких опцій CLI.
 */
typedef enum arg_code {
    ARG_NO_COLORS = 0,
    ARG_PORTRAIT = 1,
    ARG_LANDSCAPE = 2,
    ARG_MARGINS = 4,
    ARG_WIDTH = 5,
    ARG_HEIGHT = 6,
    ARG_OUTPUT = 7,
    ARG_PNG = 8,
    ARG_DRY_RUN = 9,
    ARG_VERBOSE = 10,
    ARG_LIST = 11,
    ARG_DX = 12,
    ARG_DY = 13,
    ARG_DEVICE_NAME = 14,
    ARG_SHOW = 16,
    ARG_RESET = 17,
    ARG_SET = 18,
    ARG_DEVICE_MODEL = 19,
    ARG_PREVIEW = 20,
    ARG_FORMAT = 21,
    ARG_FONT_FAMILIES = 22,
    ARG_FONT_FAMILY_VALUE = 23,
    ARG_FIT_PAGE = 24
} arg_code_t;

/**
 * @brief Опис однієї підтримуваної опції CLI.
 */
typedef struct cli_option_desc {
    const char *long_name;
    int has_arg;
    int code;
    char short_name;
    const char *arg_placeholder;
    const char *group;
    const char *description;
} cli_option_desc_t;

/**
 * @brief Опис однієї підкоманди.
 */
typedef struct cli_command_desc {
    const char *name;
    const char *description;
} cli_command_desc_t;

/**
 * @brief Тип значення конфігураційного ключа.
 */
typedef enum { CFGK_INT = 0, CFGK_DOUBLE = 1, CFGK_ENUM = 2, CFGK_ALIAS = 3 } cfg_valtype_t;

/**
 * @brief Опис одного конфігураційного ключа для `config`.
 */
typedef struct cli_config_desc {
    const char *key;
    cfg_valtype_t type;
    size_t offset;
    const char *unit;
    const char *enum_values;
    const char *description;
    const char *fmt;
} cli_config_desc_t;

/**
 * @brief Формат вхідних даних для команди `print`.
 */
typedef enum {
    INPUT_FORMAT_TEXT = 0,
    INPUT_FORMAT_MARKDOWN = 1,
} input_format_t;

/**
 * @brief Режим руху (профіль швидкість/прискорення).
 */
typedef enum {
    MOTION_PROFILE_BALANCED = 0, /**< Збалансований: якість/швидкість. */
    MOTION_PROFILE_PRECISE = 1,  /**< Точний: повільніше, менше вібрацій. */
    MOTION_PROFILE_FAST = 2,     /**< Швидкий: вище ліміти для перельотів/чернеток. */
} motion_profile_t;

typedef struct args_print_options {
    char file_name[FILE_NAME_SIZE];
    orientation_t orientation;
    double margin_top_mm;
    double margin_right_mm;
    double margin_bottom_mm;
    double margin_left_mm;
    double paper_w_mm;
    double paper_h_mm;
    bool preview;
    bool preview_png;
    char output_path[FILE_NAME_SIZE];
    bool fit_page;
    bool dry_run;
    char font_family[128];
    double font_size_pt;
    char device_model[32];
    input_format_t input_format;
    motion_profile_t motion_profile;
} args_print_options_t;

typedef struct args_device_options {
    device_action_t action;
    char remote_device[64];
    double jog_dx_mm;
    double jog_dy_mm;
    char device_model[32];
} args_device_options_t;

typedef struct args_config_options {
    config_action_t action;
    char set_pairs[512];
} args_config_options_t;

typedef struct args_fonts_options {
    bool list;
    bool list_families;
} args_fonts_options_t;

/**
 * @brief Сукупність усіх параметрів CLI, що впливають на виконання команд.
 */
struct options {
    bool help;
    bool version;
    bool no_colors;
    bool verbose;
    cmd_t cmd;
    args_print_options_t print;
    args_device_options_t device;
    args_config_options_t config;
    args_fonts_options_t fonts;
};

/**
 * @brief Синонім для структури параметрів.
 */
typedef struct options options_t;

/**
 * @brief Розбір аргументів командного рядка та заповнення структури параметрів.
 * @ingroup args
 * @param argc Кількість аргументів.
 * @param argv Масив аргументів.
 * @param options Вихідна структура з результатами розбору.
 */
void args_options_parser (int argc, char *argv[], options_t *options);

/**
 * @brief Повертає масив довгих опцій для `getopt_long`.
 */
const struct option *argdefs_long_options (void);

/**
 * @brief Повертає опис підтримуваних опцій для генерації хелпу.
 * @param out_count [out] Кількість елементів у масиві.
 */
const cli_option_desc_t *args_argdefs_options (size_t *out_count);

/**
 * @brief Повертає список підкоманд.
 * @param out_count [out] Кількість елементів у масиві.
 */
const cli_command_desc_t *args_argdefs_commands (size_t *out_count);

/**
 * @brief Повертає опис підтримуваних конфігураційних ключів.
 * @param out_count [out] Кількість елементів у масиві.
 */
const cli_config_desc_t *args_argdefs_config_keys (size_t *out_count);

#endif
