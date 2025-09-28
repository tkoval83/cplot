/**
 * @file cmd.h
 * @brief Публічний API підкоманд (без залежності від CLI/args).
 */
#ifndef CPLOT_CMD_H
#define CPLOT_CMD_H

#include "config.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Код результату виконання підкоманди (0 — успіх). */
typedef int cmd_result_t;

void cmd_set_output (FILE *out);

/** Друк/планування з буфера (без читання stdin/файлів). */
cmd_result_t cmd_print_execute (
    const char *in_chars,
    size_t in_len,
    bool markdown,
    const char *font_family,
    double font_size_pt,
    const char *device_model,
    double paper_w_mm,
    double paper_h_mm,
    double margin_top_mm,
    double margin_right_mm,
    double margin_bottom_mm,
    double margin_left_mm,
    int orientation,
    bool fit_page,
    bool dry_run,
    bool verbose);

/**
 * Згенерувати попередній перегляд розкладки.
 *
 * Обгортка над cmd_print_execute() з фіксованим режимом preview=true.
 *
 * @param in               Вміст вхідного файлу (буфер у пам'яті; не NULL при len>0).
 * @param font_family      Родина шрифтів або NULL для типового.
 * @param font_size_pt     Кегль у пунктах (≤0 — використати значення з конфігурації).
 * @param device_model     Ідентифікатор моделі AxiDraw або NULL для типової.
 * @param paper_w_mm       Ширина паперу (мм).
 * @param paper_h_mm       Висота паперу (мм).
 * @param margin_top_mm    Верхнє поле (мм).
 * @param margin_right_mm  Праве поле (мм).
 * @param margin_bottom_mm Нижнє поле (мм).
 * @param margin_left_mm   Ліве поле (мм).
 * @param orientation      Одна з констант orientation_t (див. config.h).
 * @param format           Формат прев’ю (SVG або PNG).
 * @param verbose          Рівень деталізації логів.
 * @param out              Вихідний буфер з байтами прев'ю (виділяється всередині; звільняє викликач
 * через free()).
 * @return 0 успіх; ненульовий код — помилка. На помилці out->bytes==NULL, out->len==0.
 */
cmd_result_t cmd_print_preview (
    const char *in_chars,
    size_t in_len,
    bool markdown,
    const char *font_family,
    double font_size_pt,
    const char *device_model,
    double paper_w_mm,
    double paper_h_mm,
    double margin_top_mm,
    double margin_right_mm,
    double margin_bottom_mm,
    double margin_left_mm,
    int orientation,
    int fit_page,
    int preview_png,
    bool verbose,
    uint8_t **out_bytes,
    size_t *out_len);
/** Показати версію програми. @return 0 успіх. */
cmd_result_t cmd_version_execute (bool verbose);
/** Вивести список доступних векторних шрифтів. @return 0 успіх. */
cmd_result_t cmd_fonts_execute (bool verbose);
/**
 * Перелік шрифтів (деталізована функція для підкоманди fonts).
 *
 * @param verbose Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_font_list_execute (bool families_only, bool verbose);
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
cmd_result_t
cmd_config_execute (int action, const char *set_pairs, config_t *inout_cfg, bool verbose);

/**
 * Показати конфігурацію (деталізована функція для config --show).
 *
 * @param cfg     Конфігурація для показу (не NULL).
 * @param verbose Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_config_show (const config_t *cfg, bool verbose);

/**
 * Скинути конфігурацію до типових значень (config --reset).
 *
 * @param inout_cfg Конфігурація для зміни (не NULL).
 * @param verbose   Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_config_reset (config_t *inout_cfg, bool verbose);

/**
 * Встановити значення конфігурації за парами key=value (config --set).
 *
 * @param set_pairs  Рядок пар key=value, розділених комами (не NULL).
 * @param inout_cfg  Конфігурація для зміни (не NULL).
 * @param verbose    Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_config_set (const char *set_pairs, config_t *inout_cfg, bool verbose);
/**
 * Операції над пристроєм (list/jog/pen/...)
 *
 * @param action    Дія пристрою (див. device_action_t у args.h; не NULL).
 * @param alias     Псевдонім пристрою (як у `device list`) або NULL для автопошуку.
 * @param model     Ідентифікатор моделі або NULL для типової.
 * @param jog_dx_mm Зсув по X у мм для дії jog.
 * @param jog_dy_mm Зсув по Y у мм для дії jog.
 * @param verbose   Рівень деталізації.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_device_list (const char *model, bool verbose);
cmd_result_t cmd_device_profile (const char *alias, const char *model, bool verbose);
cmd_result_t cmd_device_pen_up (const char *alias, const char *model, bool verbose);
cmd_result_t cmd_device_pen_down (const char *alias, const char *model, bool verbose);
cmd_result_t cmd_device_pen_toggle (const char *alias, const char *model, bool verbose);
cmd_result_t cmd_device_motors_on (const char *alias, const char *model, bool verbose);
cmd_result_t cmd_device_motors_off (const char *alias, const char *model, bool verbose);
cmd_result_t cmd_device_abort (const char *alias, const char *model, bool verbose);
cmd_result_t cmd_device_home (const char *alias, const char *model, bool verbose);
cmd_result_t
cmd_device_jog (const char *alias, const char *model, double dx_mm, double dy_mm, bool verbose);
cmd_result_t cmd_device_version (const char *alias, const char *model, bool verbose);
cmd_result_t cmd_device_status (const char *alias, const char *model, bool verbose);
cmd_result_t cmd_device_position (const char *alias, const char *model, bool verbose);
cmd_result_t cmd_device_reset (const char *alias, const char *model, bool verbose);
cmd_result_t cmd_device_reboot (const char *alias, const char *model, bool verbose);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CPLOT_CMD_H */
