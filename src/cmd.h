/**
 * @file cmd.h
 * @brief Скорочений фасад підкоманд (раніше commands.h).
 */
#ifndef CPLOT_CMD_H
#define CPLOT_CMD_H

#include "config.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Код результату виконання підкоманди (0 — успіх). */
typedef int cmd_result_t;

/** Формат прев’ю, що виводиться у stdout. */
typedef enum {
    PREVIEW_FMT_SVG = 0, /**< Вивести SVG */
    PREVIEW_FMT_PNG = 1  /**< Вивести PNG */
} preview_fmt_t;

/** Рівень деталізації логів. */
typedef enum {
    VERBOSE_OFF = 0, /**< Мінімальний вивід */
    VERBOSE_ON = 1   /**< Докладний вивід */
} verbose_level_t;

/**
 * Кодування тексту, що надходить на вхід.
 */
typedef enum {
    STR_ENC_UTF8 = 0, /**< UTF-8 (типово) */
    STR_ENC_ASCII = 1 /**< 7-біт ASCII */
} string_encoding_t;

/**
 * Рядок у пам'яті з явним кодуванням (вхідні дані; буфер не належить виклику).
 */
typedef struct string_view {
    const char *chars;     /**< вказівник на початок рядка (може містити NUL усередині) */
    size_t len;            /**< довжина у байтах */
    string_encoding_t enc; /**< кодування символів */
} string_t;

/**
 * Власний буфер байтів (вихідні дані; звільняється викликачем через free()).
 */
typedef struct bytes {
    uint8_t *bytes; /**< вказівник на початок буфера */
    size_t len;     /**< довжина буфера у байтах */
} bytes_t;

/**
 * Виконати друк на пристрій.
 *
 * @param in               Вміст вхідного файлу (буфер у пам'яті; не NULL при len>0).
 * @param font_family      Родина шрифтів або NULL для типового.
 * @param paper_w_mm       Ширина паперу (мм).
 * @param paper_h_mm       Висота паперу (мм).
 * @param margin_top_mm    Верхнє поле (мм).
 * @param margin_right_mm  Праве поле (мм).
 * @param margin_bottom_mm Нижнє поле (мм).
 * @param margin_left_mm   Ліве поле (мм).
 * @param orientation      Одна з констант orientation_t (див. args.h).
 * @param dry_run          Не надсилати команди на пристрій.
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
    verbose_level_t verbose);

/**
 * Згенерувати попередній перегляд розкладки.
 *
 * Обгортка над cmd_print_execute() з фіксованим режимом preview=true.
 *
 * @param in               Вміст вхідного файлу (буфер у пам'яті; не NULL при len>0).
 * @param font_family      Родина шрифтів або NULL для типового.
 * @param paper_w_mm       Ширина паперу (мм).
 * @param paper_h_mm       Висота паперу (мм).
 * @param margin_top_mm    Верхнє поле (мм).
 * @param margin_right_mm  Праве поле (мм).
 * @param margin_bottom_mm Нижнє поле (мм).
 * @param margin_left_mm   Ліве поле (мм).
 * @param orientation      Одна з констант orientation_t (див. args.h).
 * @param format           Формат прев’ю (SVG або PNG).
 * @param verbose          Рівень деталізації логів.
 * @param out              Вихідний буфер з байтами прев'ю (виділяється всередині; звільняє викликач
 * через free()).
 * @return 0 успіх; ненульовий код — помилка. На помилці out->bytes==NULL, out->len==0.
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
    bytes_t *out);
/** Показати версію програми. @return 0 успіх. */
cmd_result_t cmd_version_execute (verbose_level_t verbose);
/** Вивести список доступних векторних шрифтів. @return 0 успіх. */
cmd_result_t cmd_fonts_execute (verbose_level_t verbose);
/**
 * Перелік шрифтів (деталізована функція для підкоманди fonts).
 *
 * @param verbose Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_font_list_execute (verbose_level_t verbose);
/**
 * Операції над конфігурацією (show/reset/set).
 *
 * @param action     Дія (див. config_action_t у args.h).
 * @param set_pairs  Пара(и) key=value через кому (для action==SET) або NULL.
 * @param inout_cfg  Конфігурація для читання/зміни (може бути попередньо
 *                   заповненою значеннями за замовчуванням).
 * @param verbose    Рівень деталізації.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_config_execute (
    int action, const char *set_pairs, config_t *inout_cfg, verbose_level_t verbose);

/**
 * Показати конфігурацію (деталізована функція для config --show).
 *
 * @param cfg     Конфігурація для показу (не NULL).
 * @param verbose Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_config_show (const config_t *cfg, verbose_level_t verbose);

/**
 * Скинути конфігурацію до типових значень (config --reset).
 *
 * @param inout_cfg Конфігурація для зміни (не NULL).
 * @param verbose   Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_config_reset (config_t *inout_cfg, verbose_level_t verbose);

/**
 * Встановити значення конфігурації за парами key=value (config --set).
 *
 * @param set_pairs  Рядок пар key=value, розділених комами (не NULL).
 * @param inout_cfg  Конфігурація для зміни (не NULL).
 * @param verbose    Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_config_set (const char *set_pairs, config_t *inout_cfg, verbose_level_t verbose);
/**
 * Операції над пристроєм (list/jog/pen/...)
 *
 * @param action   Дія пристрою (див. device_action_t у args.h).
 * @param port     Шлях до серійного порту або NULL для автопошуку.
 * @param model    Ідентифікатор моделі або NULL для типової.
 * @param jog_dx_mm Зсув по X у мм для дії jog.
 * @param jog_dy_mm Зсув по Y у мм для дії jog.
 * @param verbose  Рівень деталізації.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_device_execute (
    int action,
    const char *port,
    const char *model,
    double jog_dx_mm,
    double jog_dy_mm,
    verbose_level_t verbose);
/** Вивести діагностичну інформацію системи. @return 0 успіх. */
cmd_result_t cmd_sysinfo_execute (verbose_level_t verbose);

/* ---- Деталізовані дії пристрою (відповідають device_action_t) ---- */
/**
 * Перелічити доступні порти.
 *
 * @param model   Ідентифікатор моделі або NULL для типової.
 * @param verbose Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_device_list (const char *model, verbose_level_t verbose);
/**
 * Підняти перо.
 *
 * @param port    Шлях до серійного порту або NULL для автопошуку.
 * @param model   Ідентифікатор моделі або NULL для типової.
 * @param verbose Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_device_pen_up (const char *port, const char *model, verbose_level_t verbose);
/**
 * Опустити перо.
 *
 * @param port    Шлях до серійного порту або NULL для автопошуку.
 * @param model   Ідентифікатор моделі або NULL для типової.
 * @param verbose Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_device_pen_down (const char *port, const char *model, verbose_level_t verbose);
/**
 * Перемкнути стан пера.
 *
 * @param port    Шлях до серійного порту або NULL для автопошуку.
 * @param model   Ідентифікатор моделі або NULL для типової.
 * @param verbose Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_device_pen_toggle (const char *port, const char *model, verbose_level_t verbose);
/**
 * Увімкнути мотори.
 *
 * @param port    Шлях до серійного порту або NULL для автопошуку.
 * @param model   Ідентифікатор моделі або NULL для типової.
 * @param verbose Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_device_motors_on (const char *port, const char *model, verbose_level_t verbose);
/**
 * Вимкнути мотори.
 *
 * @param port    Шлях до серійного порту або NULL для автопошуку.
 * @param model   Ідентифікатор моделі або NULL для типової.
 * @param verbose Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_device_motors_off (const char *port, const char *model, verbose_level_t verbose);
/**
 * Поїхати у home.
 *
 * @param port    Шлях до серійного порту або NULL для автопошуку.
 * @param model   Ідентифікатор моделі або NULL для типової.
 * @param verbose Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_device_home (const char *port, const char *model, verbose_level_t verbose);
/**
 * Ручний зсув (jog).
 *
 * @param port    Шлях до серійного порту або NULL для автопошуку.
 * @param model   Ідентифікатор моделі або NULL для типової.
 * @param dx_mm   Зсув по X, мм.
 * @param dy_mm   Зсув по Y, мм.
 * @param verbose Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_device_jog (
    const char *port, const char *model, double dx_mm, double dy_mm, verbose_level_t verbose);
/**
 * Запит версії прошивки пристрою.
 *
 * @param port    Шлях до серійного порту або NULL для автопошуку.
 * @param model   Ідентифікатор моделі або NULL для типової.
 * @param verbose Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_device_version (const char *port, const char *model, verbose_level_t verbose);
/**
 * Запит статусу пристрою.
 *
 * @param port    Шлях до серійного порту або NULL для автопошуку.
 * @param model   Ідентифікатор моделі або NULL для типової.
 * @param verbose Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_device_status (const char *port, const char *model, verbose_level_t verbose);
/**
 * Запит позиції координат.
 *
 * @param port    Шлях до серійного порту або NULL для автопошуку.
 * @param model   Ідентифікатор моделі або NULL для типової.
 * @param verbose Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_device_position (const char *port, const char *model, verbose_level_t verbose);
/**
 * Скидання контролера.
 *
 * @param port    Шлях до серійного порту або NULL для автопошуку.
 * @param model   Ідентифікатор моделі або NULL для типової.
 * @param verbose Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_device_reset (const char *port, const char *model, verbose_level_t verbose);
/**
 * Перезавантаження контролера.
 *
 * @param port    Шлях до серійного порту або NULL для автопошуку.
 * @param model   Ідентифікатор моделі або NULL для типової.
 * @param verbose Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_device_reboot (const char *port, const char *model, verbose_level_t verbose);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CPLOT_CMD_H */
