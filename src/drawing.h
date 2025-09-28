/**
 * @file drawing.h
 * @brief Перетворення текстового вводу на геометрію, превʼю та плани руху.
 */
#ifndef CPLOT_DRAWING_H
#define CPLOT_DRAWING_H

#include "canvas.h"
#include <stddef.h>
#include <stdint.h>
#include "planner.h"
#include "text.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Параметри сторінки та полів для розкладки.
 */
typedef struct {
    double paper_w_mm;
    double paper_h_mm;
    double margin_top_mm;
    double margin_right_mm;
    double margin_bottom_mm;
    double margin_left_mm;
    orientation_t orientation;
} drawing_page_t;

/**
 * @brief Результат побудови розкладки (шляхи у мм + метадані).
 */
typedef struct {
    canvas_layout_t layout;       /**< Геометрія у координатах пристрою (мм). */
    text_render_info_t text_info; /**< Статистика рендерингу тексту. */
} drawing_layout_t;

/* Локальні базові типи вводу/виводу для модуля рендерингу */
typedef enum {
    STR_ENC_UTF8 = 0,
    STR_ENC_ASCII = 1
} string_encoding_t;

typedef struct string_view {
    const char *chars;
    size_t len;
    string_encoding_t enc;
} string_t;

typedef struct bytes {
    uint8_t *bytes;
    size_t len;
} bytes_t;

/**
 * @brief Побудувати розкладку тексту у координатах пристрою.
 *
 * @param page        Параметри сторінки.
 * @param font_family Родина шрифтів або NULL.
 * @param font_size_pt Кегль у пунктах (≤0 — типове значення).
 * @param input       Текст для рендерингу.
 * @param layout      Вихідна структура з геометрією.
 * @return 0 успіх; ненульовий код — помилка.
 */
int drawing_build_layout (
    const drawing_page_t *page,
    const char *font_family,
    double font_size_pt,
    string_t input,
    drawing_layout_t *layout);

/**
 * @brief Побудувати розкладку з уже наявних шляхів (напр., з фігур Bézier).
 *
 * @param page         Параметри сторінки та полів (не NULL).
 * @param source_paths Вхідні шляхи у координатах `units` (типово мм).
 * @param layout       Вихідна структура з геометрією (не NULL).
 * @return 0 успіх; 2 — некоректні параметри полотна; 1 — помилка памʼяті.
 */
int drawing_build_layout_from_paths (
    const drawing_page_t *page, const geom_paths_t *source_paths, drawing_layout_t *layout);

/**
 * @brief Звільнити ресурси, повʼязані з розкладкою.
 *
 * @param layout Структура для очищення.
 */
void drawing_layout_dispose (drawing_layout_t *layout);

/**
 * @brief Згенерувати SVG-превʼю для готової розкладки.
 *
 * @param layout Розкладка з геометрією.
 * @param out    Вихідний буфер із байтами SVG.
 * @return 0 успіх; ненульовий код — помилка.
 */
int drawing_preview_svg (const drawing_layout_t *layout, bytes_t *out);

/**
 * @brief Згенерувати PNG-превʼю для готової розкладки.
 *
 * @param layout Розкладка з геометрією.
 * @param out    Вихідний буфер із байтами PNG.
 * @return 0 успіх; ненульовий код — помилка.
 */
int drawing_preview_png (const drawing_layout_t *layout, bytes_t *out);

/**
 * @brief Сформувати план руху, використовуючи planner_plan().
 *
 * @param layout     Розкладка з геометрією.
 * @param limits     Обмеження планувальника або NULL.
 * @param out_blocks Вихідний масив блоків (виділяється).
 * @param out_count  Вихід: кількість блоків.
 * @return 0 успіх; ненульовий код — помилка.
 */
int drawing_generate_motion_plan (
    const drawing_layout_t *layout,
    const planner_limits_t *limits,
    plan_block_t **out_blocks,
    size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* CPLOT_DRAWING_H */
