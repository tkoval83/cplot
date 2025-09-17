/**
 * @file cmd.c
 * @brief Реалізації фасаду підкоманд (скорочена назва файлу).
 */
#include "cmd.h"
#include "axidraw.h"
#include "fontreg.h"
#include "help.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Сконвертувати config_t у налаштування AxiDraw.
 *
 * @param out Налаштування для заповнення (не NULL).
 * @param cfg Конфігурація CLI (може бути NULL).
 */
static void axidraw_settings_from_config (axidraw_settings_t *out, const config_t *cfg) {
    if (!out)
        return;
    axidraw_settings_reset (out);
    if (!cfg)
        return;
    out->pen_up_delay_ms = cfg->pen_up_delay_ms;
    out->pen_down_delay_ms = cfg->pen_down_delay_ms;
    out->pen_up_pos = cfg->pen_up_pos;
    out->pen_down_pos = cfg->pen_down_pos;
    out->pen_up_speed = cfg->pen_up_speed;
    out->pen_down_speed = cfg->pen_down_speed;
    out->servo_timeout_s = cfg->servo_timeout_s;
    out->speed_mm_s = cfg->speed_mm_s;
    out->accel_mm_s2 = cfg->accel_mm_s2;
}

/**
 * @brief Завантажити та підготувати налаштування для вказаної моделі пристрою.
 *
 * @param model    Ідентифікатор моделі або NULL для типового.
 * @param settings Вихідна структура налаштувань (не NULL).
 * @return true, якщо налаштування отримано; false при помилці.
 */
static bool load_axidraw_settings (const char *model, axidraw_settings_t *settings) {
    if (!settings)
        return false;
    const char *model_id = (model && *model) ? model : CONFIG_DEFAULT_MODEL;
    config_t cfg;
    if (config_factory_defaults (&cfg, model_id) != 0) {
        LOGW ("Не вдалося завантажити заводські налаштування для моделі %s", model_id);
        axidraw_settings_reset (settings);
        return false;
    }
    axidraw_settings_from_config (settings, &cfg);
    return true;
}

/**
 * @brief Підготувати налаштування та зафіксувати їх у логах для конкретної дії.
 *
 * Функція використовує load_axidraw_settings(), застосовує їх до тимчасового пристрою
 * і виводить налагоджувальний запис у разі увімкненого verbose.
 *
 * @param action  Людина-зрозумілий ідентифікатор дії (для логів).
 * @param model   Назва моделі пристрою або NULL.
 * @param verbose Рівень деталізації логів.
 */
static void
trace_axidraw_settings (const char *action, const char *model, verbose_level_t verbose) {
    axidraw_settings_t settings;
    if (!load_axidraw_settings (model, &settings))
        return;

    axidraw_device_t dev;
    axidraw_device_init (&dev);
    axidraw_apply_settings (&dev, &settings);

    if (verbose == VERBOSE_ON) {
        LOGD (
            "axidraw[%s]: інтервал %.2f мс, fifo %zu, затримки пера %d/%d мс", action,
            settings.min_cmd_interval_ms, settings.fifo_limit, settings.pen_up_delay_ms,
            settings.pen_down_delay_ms);
    }
}

/**
 * Виконати підкоманду print (побудова розкладки та відправлення на пристрій).
 *
 * Контракт:
 * - file_path: шлях до вхідного файлу або "-"/NULL для stdin.
 * - Якщо dry_run=true, фактична взаємодія з пристроєм не виконується, лише лог та підготовка.
 * - verbose керує рівнем деталізації журналу (див. verbose_level_t).
 * - На даному етапі реалізація є заглушкою і повертає успіх.
 *
 * @param file_path        Шлях до вхідного файлу або NULL/"-" для stdin.
 * @param font_family      Родина шрифтів або NULL для типової.
 * @param paper_w_mm       Ширина паперу (мм).
 * @param paper_h_mm       Висота паперу (мм).
 * @param margin_top_mm    Верхнє поле (мм).
 * @param margin_right_mm  Праве поле (мм).
 * @param margin_bottom_mm Нижнє поле (мм).
 * @param margin_left_mm   Ліве поле (мм).
 * @param orientation      Значення з enum orientation_t (див. args.h): 1 портрет, 2 альбом.
 * @param dry_run          Імітація без реального виконання.
 * @param verbose          Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_print_execute (
    string_t in,
    const char *font_family,
    double paper_w_mm,
    double paper_h_mm,
    double margin_top_mm,
    double margin_right_mm,
    double margin_bottom_mm,
    double margin_left_mm,
    int orientation,
    bool dry_run,
    verbose_level_t verbose) {
    LOGI ("Почато побудову та друк");
    (void)paper_w_mm;
    (void)paper_h_mm;
    (void)margin_top_mm;
    (void)margin_right_mm;
    (void)margin_bottom_mm;
    (void)margin_left_mm;
    (void)in;
    (void)font_family;
    (void)orientation;
    (void)dry_run;
    (void)verbose;
    fprintf (stdout, "Друк: ще не реалізовано\n");
    return 0;
}

/**
 * Обгортка для попереднього перегляду: виклик cmd_print_execute у режимі preview.
 */
cmd_result_t cmd_print_preview_execute (
    string_t in,
    const char *font_family,
    double paper_w_mm,
    double paper_h_mm,
    double margin_top_mm,
    double margin_right_mm,
    double margin_bottom_mm,
    double margin_left_mm,
    int orientation,
    preview_fmt_t format,
    verbose_level_t verbose,
    bytes_t *out) {
    (void)in;
    (void)font_family;
    (void)paper_w_mm;
    (void)paper_h_mm;
    (void)margin_top_mm;
    (void)margin_right_mm;
    (void)margin_bottom_mm;
    (void)margin_left_mm;
    (void)orientation;
    (void)verbose;
    LOGI ("Попередній перегляд");
    if (!out) {
        LOGE ("Внутрішня помилка: не задано вихідні параметри прев’ю");
        return 1;
    }
    out->bytes = NULL;
    out->len = 0;

    /* Тимчасова заглушка: повертаємо мінімальний валідний SVG або PNG-заглушку. */
    if (format == PREVIEW_FMT_SVG) {
        static const char svg_stub[]
            = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"50\">"
              "<text x=\"10\" y=\"25\" font-family=\"sans-serif\" font-size=\"12\">"
              "прев’ю ще не реалізовано"
              "</text></svg>";
        size_t n = sizeof (svg_stub) - 1;
        uint8_t *buf = (uint8_t *)malloc (n);
        if (!buf) {
            LOGE ("Недостатньо пам'яті для формування прев’ю");
            return 1;
        }
        memcpy (buf, svg_stub, n);
        out->bytes = buf;
        out->len = n;
        return 0;
    } else {
        /* PNG-заглушка: найменший валідний PNG (однопіксельний) у вигляді константного масиву */
        static const uint8_t png_stub[]
            = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48,
                0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00,
                0x00, 0x90, 0x77, 0x53, 0xDE, 0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41, 0x54, 0x78,
                0x9C, 0x63, 0x60, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0xE5, 0x27, 0xD4, 0xA2, 0x00,
                0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };
        size_t n = sizeof (png_stub);
        uint8_t *buf = (uint8_t *)malloc (n);
        if (!buf) {
            LOGE ("Недостатньо пам'яті для формування прев’ю");
            return 1;
        }
        memcpy (buf, png_stub, n);
        out->bytes = buf;
        out->len = n;
        return 0;
    }
}

/**
 * Згенерувати прев’ю розкладки у stdout (SVG або PNG).
 *
 * Контракт:
 * - Не здійснює взаємодії з пристроєм; лише побудова розкладки та вивід у stdout.
 * - Формат виводу задається параметром format.
 * - На даному етапі реалізація є заглушкою і повертає успіх.
 *
 * @param file_path        Шлях до вхідного файлу або NULL/"-" для stdin.
 * @param font_family      Родина шрифтів або NULL.
 * @param paper_w_mm       Ширина паперу (мм).
 * @param paper_h_mm       Висота паперу (мм).
 * @param margin_top_mm    Верхнє поле (мм).
 * @param margin_right_mm  Праве поле (мм).
 * @param margin_bottom_mm Нижнє поле (мм).
 * @param margin_left_mm   Ліве поле (мм).
 * @param orientation      Значення з enum orientation_t (див. args.h): 1 портрет, 2 альбом.
 * @param dry_run          Імітація без реального виконання (для симетрії з print).
 * @param format           Формат прев’ю (SVG або PNG).
 * @param verbose          Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
/* видалено окрему реалізацію прев'ю; інтегровано у cmd_print_execute */

/**
 * Показати версію програми.
 *
 * @param verbose Рівень деталізації логів.
 * @return 0 успіх.
 */
cmd_result_t cmd_version_execute (verbose_level_t verbose) {
    if (verbose)
        LOGI ("Докладний режим виводу");
    version ();
    return 0;
}

/**
 * Вивести перелік доступних векторних шрифтів.
 *
 * @param verbose Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_fonts_execute (verbose_level_t verbose) { return cmd_font_list_execute (verbose); }

/**
 * Операції над конфігурацією (show/reset/set).
 *
 * @param action     Дія (див. config_action_t у args.h).
 * @param set_pairs  Пара(и) key=value через кому (для action==SET) або NULL.
 * @param inout_cfg  Конфігурація для читання/зміни.
 * @param verbose    Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_config_execute (
    int action, const char *set_pairs, config_t *inout_cfg, verbose_level_t verbose) {
    switch (action) {
    case 0:
        LOGW ("Не вказано дію для налаштувань");
        return 2;
    case 1: /* CFG_SHOW */
        return cmd_config_show (inout_cfg, verbose);
    case 2: /* CFG_RESET */
        return cmd_config_reset (inout_cfg, verbose);
    case 3: /* CFG_SET */
        return cmd_config_set (set_pairs ? set_pairs : "", inout_cfg, verbose);
    default:
        LOGW ("Невідома дія для налаштувань");
        return 2;
    }
}

/**
 * Операції над пристроєм (list/jog/pen/...)
 *
 * @param action   Дія пристрою (див. device_action_t у args.h).
 * @param port     Шлях до серійного порту або NULL для автопошуку.
 * @param model    Ідентифікатор моделі або NULL для типової.
 * @param jog_dx_mm Зсув по X у мм для дії jog.
 * @param jog_dy_mm Зсув по Y у мм для дії jog.
 * @param verbose  Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_device_execute (
    const device_action_t *action,
    const char *port,
    const char *model,
    double jog_dx_mm,
    double jog_dy_mm,
    verbose_level_t verbose) {
    if (!action || action->kind == DEVICE_ACTION_NONE) {
        LOGW ("Не вказано дію для пристрою");
        return 2;
    }

    switch (action->kind) {
    case DEVICE_ACTION_LIST:
        return cmd_device_list (model, verbose);
    case DEVICE_ACTION_PEN:
        switch (action->pen) {
        case DEVICE_PEN_UP:
            return cmd_device_pen_up (port, model, verbose);
        case DEVICE_PEN_DOWN:
            return cmd_device_pen_down (port, model, verbose);
        case DEVICE_PEN_TOGGLE:
            return cmd_device_pen_toggle (port, model, verbose);
        default:
            LOGW ("Невідома дія пера");
            return 2;
        }
    case DEVICE_ACTION_MOTORS:
        switch (action->motor) {
        case DEVICE_MOTOR_ON:
            return cmd_device_motors_on (port, model, verbose);
        case DEVICE_MOTOR_OFF:
            return cmd_device_motors_off (port, model, verbose);
        default:
            LOGW ("Невідома дія моторів");
            return 2;
        }
    case DEVICE_ACTION_HOME:
        return cmd_device_home (port, model, verbose);
    case DEVICE_ACTION_JOG:
        return cmd_device_jog (port, model, jog_dx_mm, jog_dy_mm, verbose);
    case DEVICE_ACTION_VERSION:
        return cmd_device_version (port, model, verbose);
    case DEVICE_ACTION_STATUS:
        return cmd_device_status (port, model, verbose);
    case DEVICE_ACTION_POSITION:
        return cmd_device_position (port, model, verbose);
    case DEVICE_ACTION_RESET:
        return cmd_device_reset (port, model, verbose);
    case DEVICE_ACTION_REBOOT:
        return cmd_device_reboot (port, model, verbose);
    default:
        LOGW ("Невідома категорія дій пристрою");
        return 2;
    }
}

/**
 * Вивести діагностичну інформацію системи.
 *
 * @param verbose Рівень деталізації логів.
 * @return 0 успіх.
 */
cmd_result_t cmd_sysinfo_execute (verbose_level_t verbose) {
    (void)verbose;
    LOGI ("Системна інформація");
    fprintf (stdout, "Системна інформація: ще не реалізовано\n");
    return 0;
}

/* ---- Реалізації деталізованих функцій (заглушки з логами) ---- */

/// Вивести перелік доступних векторних шрифтів (деталізована функція).
cmd_result_t cmd_font_list_execute (verbose_level_t verbose) {
    (void)verbose;
    LOGI ("Перелік доступних шрифтів");
    font_face_t *faces = NULL;
    size_t count = 0;
    int rc = fontreg_list (&faces, &count);
    if (rc != 0) {
        fprintf (stdout, "Не вдалося завантажити реєстр шрифтів (код %d)\n", rc);
        return 1;
    }

    fprintf (stdout, "Доступні шрифти (%zu):\n", count);
    for (size_t i = 0; i < count; ++i) {
        fprintf (stdout, "  - %-24s — %s\n", faces[i].id, faces[i].name);
    }
    free (faces);
    return 0;
}

/// Показати поточну конфігурацію (config --show).
cmd_result_t cmd_config_show (const config_t *cfg, verbose_level_t verbose) {
    (void)cfg;
    (void)verbose;
    LOGI ("Поточні налаштування");
    fprintf (stdout, "Налаштування (показ): ще не реалізовано\n");
    return 0;
}

/// Скинути конфігурацію до типових значень (config --reset).
cmd_result_t cmd_config_reset (config_t *inout_cfg, verbose_level_t verbose) {
    (void)inout_cfg;
    (void)verbose;
    LOGI ("Скидання налаштувань");
    fprintf (stdout, "Налаштування (скидання): ще не реалізовано\n");
    return 0;
}

/// Встановити значення конфігурації за парами key=value (config --set).
cmd_result_t cmd_config_set (const char *set_pairs, config_t *inout_cfg, verbose_level_t verbose) {
    (void)inout_cfg;
    (void)verbose;
    (void)set_pairs;
    LOGI ("Застосування нових налаштувань");
    fprintf (stdout, "Налаштування (встановлення): ще не реалізовано\n");
    return 0;
}

/// Перелічити доступні порти пристроїв (device --list).
cmd_result_t cmd_device_list (const char *model, verbose_level_t verbose) {
    (void)verbose;
    (void)model;
    LOGI ("Перелік доступних портів");
    fprintf (stdout, "Порти: ще не реалізовано\n");
    return 0;
}

/// Підняти перо (device up).
cmd_result_t cmd_device_pen_up (const char *port, const char *model, verbose_level_t verbose) {
    (void)port;
    trace_axidraw_settings ("pen_up", model, verbose);
    LOGI ("Підйом пера");
    fprintf (stdout, "Перо вгору: ще не реалізовано\n");
    return 0;
}

/// Опустити перо (device down).
cmd_result_t cmd_device_pen_down (const char *port, const char *model, verbose_level_t verbose) {
    (void)port;
    trace_axidraw_settings ("pen_down", model, verbose);
    LOGI ("Опускання пера");
    fprintf (stdout, "Перо вниз: ще не реалізовано\n");
    return 0;
}

/// Перемкнути стан пера (device toggle).
cmd_result_t cmd_device_pen_toggle (const char *port, const char *model, verbose_level_t verbose) {
    (void)port;
    trace_axidraw_settings ("pen_toggle", model, verbose);
    LOGI ("Перемикання пера");
    fprintf (stdout, "Перемикання пера: ще не реалізовано\n");
    return 0;
}

/// Увімкнути мотори (device motors-on).
cmd_result_t cmd_device_motors_on (const char *port, const char *model, verbose_level_t verbose) {
    (void)port;
    trace_axidraw_settings ("motors_on", model, verbose);
    LOGI ("Увімкнення моторів");
    fprintf (stdout, "Мотори увімкнено: ще не реалізовано\n");
    return 0;
}

/// Вимкнути мотори (device motors-off).
cmd_result_t cmd_device_motors_off (const char *port, const char *model, verbose_level_t verbose) {
    (void)port;
    trace_axidraw_settings ("motors_off", model, verbose);
    LOGI ("Вимкнення моторів");
    fprintf (stdout, "Мотори вимкнено: ще не реалізовано\n");
    return 0;
}

/// Перемістити у початкову позицію (home).
cmd_result_t cmd_device_home (const char *port, const char *model, verbose_level_t verbose) {
    (void)port;
    trace_axidraw_settings ("home", model, verbose);
    LOGI ("Повернення у початкову позицію");
    fprintf (stdout, "Домашня позиція: ще не реалізовано\n");
    return 0;
}

/// Ручний зсув на dx/dy у мм (jog).
cmd_result_t cmd_device_jog (
    const char *port, const char *model, double dx_mm, double dy_mm, verbose_level_t verbose) {
    (void)port;
    trace_axidraw_settings ("jog", model, verbose);
    LOGI ("Ручний зсув: по іксу %.3f мм, по ігреку %.3f мм", dx_mm, dy_mm);
    fprintf (stdout, "Ручний зсув: ще не реалізовано\n");
    return 0;
}

/// Показати версію контролера (device version).
cmd_result_t cmd_device_version (const char *port, const char *model, verbose_level_t verbose) {
    (void)port;
    trace_axidraw_settings ("version", model, verbose);
    LOGI ("Версія контролера");
    fprintf (stdout, "Версія: ще не реалізовано\n");
    return 0;
}

/// Показати статус пристрою (device status).
cmd_result_t cmd_device_status (const char *port, const char *model, verbose_level_t verbose) {
    (void)port;
    trace_axidraw_settings ("status", model, verbose);
    LOGI ("Стан пристрою");
    fprintf (stdout, "Стан: ще не реалізовано\n");
    return 0;
}

/// Показати поточну позицію (device position).
cmd_result_t cmd_device_position (const char *port, const char *model, verbose_level_t verbose) {
    (void)port;
    trace_axidraw_settings ("position", model, verbose);
    LOGI ("Поточна позиція");
    fprintf (stdout, "Позиція: ще не реалізовано\n");
    return 0;
}

/// Скинути контролер (device reset).
cmd_result_t cmd_device_reset (const char *port, const char *model, verbose_level_t verbose) {
    (void)port;
    trace_axidraw_settings ("reset", model, verbose);
    LOGI ("Скидання контролера");
    fprintf (stdout, "Скидання: ще не реалізовано\n");
    return 0;
}

/// Перезавантажити контролер (device reboot).
cmd_result_t cmd_device_reboot (const char *port, const char *model, verbose_level_t verbose) {
    (void)port;
    trace_axidraw_settings ("reboot", model, verbose);
    LOGI ("Перезавантаження контролера");
    fprintf (stdout, "Перезавантаження: ще не реалізовано\n");
    return 0;
}
