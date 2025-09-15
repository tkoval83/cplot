/**
 * @file cmd.h
 * @brief Скорочений фасад підкоманд (раніше commands.h).
 */
#ifndef CPLOT_CMD_H
#define CPLOT_CMD_H

#include "config.h"
#include <stdbool.h>
#include <stddef.h>

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
 * Виконати підкоманду print (плотинг на пристрій).
 *
 * @param file_path    Шлях до вхідного файлу або NULL/"-" для stdin.
 * @param font_family  Родина шрифтів або NULL для типового.
 * @param paper_w_mm   Ширина паперу (мм).
 * @param paper_h_mm   Висота паперу (мм).
 * @param margin_top_mm    Верхнє поле (мм).
 * @param margin_right_mm  Праве поле (мм).
 * @param margin_bottom_mm Нижнє поле (мм).
 * @param margin_left_mm   Ліве поле (мм).
 * @param orientation  Одна з констант orientation_t (див. args.h).
 * @param dry_run      Не надсилати команди на пристрій.
 * @param verbose      Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_print_execute (
    const char *file_path,
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
 * Згенерувати прев’ю розкладки у stdout.
 *
 * @param file_path    Шлях до вхідного файлу або NULL/"-" для stdin.
 * @param font_family  Родина шрифтів або NULL.
 * @param paper_w_mm   Ширина паперу (мм).
 * @param paper_h_mm   Висота паперу (мм).
 * @param margin_top_mm    Верхнє поле (мм).
 * @param margin_right_mm  Праве поле (мм).
 * @param margin_bottom_mm Нижнє поле (мм).
 * @param margin_left_mm   Ліве поле (мм).
 * @param orientation  Одна з констант orientation_t (див. args.h).
 * @param dry_run      Імітувати без реального виконання (для відладки).
 * @param format       Формат виводу прев’ю (SVG або PNG).
 * @param verbose      Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_preview_execute (
    const char *file_path,
    const char *font_family,
    double paper_w_mm,
    double paper_h_mm,
    double margin_top_mm,
    double margin_right_mm,
    double margin_bottom_mm,
    double margin_left_mm,
    int orientation,
    bool dry_run,
    preview_fmt_t format,
    verbose_level_t verbose);
/** Показати версію програми. @return 0 успіх. */
cmd_result_t cmd_version_execute (verbose_level_t verbose);
/** Вивести список доступних векторних шрифтів. @return 0 успіх. */
cmd_result_t cmd_fonts_execute (verbose_level_t verbose);
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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CPLOT_CMD_H */
