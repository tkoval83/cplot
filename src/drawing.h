/**
 * @file drawing.h
 * @brief Побудова розкладки, превʼю SVG/PNG та плану руху.
 * @defgroup drawing Візуалізація
 */
#ifndef CPLOT_DRAWING_H
#define CPLOT_DRAWING_H

#include "canvas.h"
#include "planner.h"
#include "text.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Параметри сторінки та полів та орієнтації.
 */
typedef struct {
    double paper_w_mm;         /**< Ширина паперу, мм. */
    double paper_h_mm;         /**< Висота паперу, мм. */
    double margin_top_mm;      /**< Верхнє поле, мм. */
    double margin_right_mm;    /**< Праве поле, мм. */
    double margin_bottom_mm;   /**< Нижнє поле, мм. */
    double margin_left_mm;     /**< Ліве поле, мм. */
    orientation_t orientation; /**< Орієнтація сторінки. */
    int fit_to_frame;          /**< 1 — масштабувати вміст під рамку. */
} drawing_page_t;

/**
 * @brief Результат побудови розкладки для рендерингу.
 */
typedef struct {
    canvas_layout_t layout;       /**< Розміщені контури у мм та рамка. */
    text_render_info_t text_info; /**< Відомості про рендеринг тексту. */
} drawing_layout_t;

/**
 * @brief Підтримувані кодування рядків вводу.
 */
typedef enum {
    STR_ENC_UTF8 = 0, /**< Вхідний текст — UTF-8. */
    STR_ENC_ASCII = 1 /**< Вхідний текст — ASCII. */
} string_encoding_t;

/**
 * @brief Представлення рядка без копіювання.
 */
typedef struct string_view {
    const char *chars;     /**< Вказівник на символи (може не бути завершено \0). */
    size_t len;            /**< Довжина у байтах. */
    string_encoding_t enc; /**< Кодування. */
} string_t;

/**
 * @brief Буфер байтів для виводу SVG/PNG.
 */
typedef struct bytes {
    uint8_t *bytes; /**< Вказівник на байти (mallocʼені). */
    size_t len;     /**< Довжина буфера. */
} bytes_t;

/**
 * @brief Побудова розкладки на основі тексту та параметрів сторінки.
 * @param page Параметри сторінки.
 * @param font_family Родина шрифту.
 * @param font_size_pt Розмір шрифту у пунктах.
 * @param input Вхідний рядок.
 * @param layout [out] Результуюча розкладка.
 * @return 0 — успіх, інакше — код помилки.
 */
int drawing_build_layout (
    const drawing_page_t *page,
    const char *font_family,
    double font_size_pt,
    string_t input,
    drawing_layout_t *layout);

/**
 * @brief Побудова розкладки з уже готових контурів.
 * @param page Параметри сторінки.
 * @param source_paths Вхідні контури (у мм або ін. — конвертуються).
 * @param layout [out] Результуюча розкладка.
 * @return 0 — успіх, інакше — код помилки.
 */
int drawing_build_layout_from_paths (
    const drawing_page_t *page, const geom_paths_t *source_paths, drawing_layout_t *layout);

/**
 * @brief Вивільняє ресурси розкладки.
 */
void drawing_layout_dispose (drawing_layout_t *layout);

/**
 * @brief Генерує SVG превʼю.
 * @param layout Розкладка.
 * @param out [out] Байти SVG (mallocʼяться всередині).
 * @return 0 — успіх, інакше помилка.
 */
int drawing_preview_svg (const drawing_layout_t *layout, bytes_t *out);

/**
 * @brief Генерує PNG превʼю.
 * @param layout Розкладка.
 * @param out [out] Байти PNG (mallocʼяться всередині).
 * @return 0 — успіх, інакше помилка.
 */
int drawing_preview_png (const drawing_layout_t *layout, bytes_t *out);

/**
 * @brief Генерує план руху для графопобудовника з розкладки.
 * @param layout Джерело шляхів у мм.
 * @param limits Обмеження планувальника (NULL — з конфігурації).
 * @param out_blocks [out] Масив блоків руху.
 * @param out_count [out] Кількість блоків.
 * @return 0 — успіх, інакше помилка.
 */

#ifdef __cplusplus
}
#endif

#endif
