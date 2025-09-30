/**
 * @file drawing.c
 * @brief Реалізація побудови розкладки та превʼю (SVG/PNG).
 * @ingroup drawing
 */

#include "drawing.h"

#include "config.h"
#include "log.h"
#include "png.h"
#include "svg.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Рендерить текст у контури з урахуванням ширини рамки.
 * @param input Вхідний текст.
 * @param font_family Родина шрифтів (може бути NULL для типових).
 * @param font_size_pt Кегль, пт (<=0 — типове значення).
 * @param frame_width_mm Ширина рамки для верстки, мм.
 * @param out_paths [out] Контури (у мм).
 * @param info [out] Інформація про рендеринг (може бути NULL).
 * @return 0 — успіх, 1 — помилка.
 */
static int drawing_build_text_paths (
    string_t input,
    const char *font_family,
    double font_size_pt,
    double frame_width_mm,
    geom_paths_t *out_paths,
    text_render_info_t *info) {
    if (!out_paths)
        return 1;
    char *text_buf = NULL;
    if (input.len > 0) {
        text_buf = (char *)malloc (input.len + 1);
        if (!text_buf)
            return 1;
        memcpy (text_buf, input.chars, input.len);
        text_buf[input.len] = '\0';
    }

    double size_pt = (font_size_pt > 0.0) ? font_size_pt : 14.0;
    text_layout_opts_t opts = {
        .family = font_family,
        .size_pt = size_pt,
        .style_flags = TEXT_STYLE_NONE,
        .units = GEOM_UNITS_MM,
        .frame_width = frame_width_mm,
        .align = TEXT_ALIGN_LEFT,
        .hyphenate = 1,
        .line_spacing = 1.0,
    };

    text_render_info_t local_info;
    text_render_info_t *info_ptr = info ? info : &local_info;
    int rc = text_layout_render (text_buf ? text_buf : "", &opts, out_paths, NULL, NULL, info_ptr);
    free (text_buf);
    if (rc != 0)
        LOGE ("Не вдалося сформувати контури тексту");
    return rc == 0 ? 0 : 1;
}

/**
 * @brief Побудова розкладки на основі тексту.
 * @param page Параметри сторінки.
 * @param font_family Родина шрифтів.
 * @param font_size_pt Розмір шрифту, пт.
 * @param input Вхідний текст.
 * @param layout [out] Розкладка.
 * @return 0 — успіх, інакше помилка.
 */
int drawing_build_layout (
    const drawing_page_t *page,
    const char *font_family,
    double font_size_pt,
    string_t input,
    drawing_layout_t *layout) {
    if (!page || !layout)
        return 1;

    double frame_width_mm = 0.0;
    if (page->orientation == ORIENT_PORTRAIT)
        frame_width_mm = page->paper_h_mm - page->margin_top_mm - page->margin_bottom_mm;
    else
        frame_width_mm = page->paper_w_mm - page->margin_left_mm - page->margin_right_mm;
    if (!(frame_width_mm > 0.0)) {
        LOGE ("Недостатня доступна ширина для тексту — перевірте поля та орієнтацію");
        return 1;
    }

    geom_paths_t text_paths;
    text_render_info_t info;
    memset (&info, 0, sizeof (info));
    if (drawing_build_text_paths (
            input, font_family, font_size_pt, frame_width_mm, &text_paths, &info)
        != 0)
        return 1;

    canvas_options_t canvas_opts = {
        .paper_w_mm = page->paper_w_mm,
        .paper_h_mm = page->paper_h_mm,
        .margin_top_mm = page->margin_top_mm,
        .margin_right_mm = page->margin_right_mm,
        .margin_bottom_mm = page->margin_bottom_mm,
        .margin_left_mm = page->margin_left_mm,
        .orientation = page->orientation,
        .font_family = font_family,
        .fit_to_frame = page->fit_to_frame ? true : false,
    };

    canvas_layout_t layout_mm;
    canvas_status_t canvas_rc = canvas_layout_document (&canvas_opts, &text_paths, &layout_mm);
    geom_paths_free (&text_paths);
    if (canvas_rc == CANVAS_STATUS_INVALID_INPUT) {
        LOGE ("Некоректні параметри полотна — перевірте орієнтацію чи поля");
        return 2;
    }
    if (canvas_rc != CANVAS_STATUS_OK) {
        LOGE ("Помилка під час планування полотна (код %d)", (int)canvas_rc);
        return 1;
    }

    layout->layout = layout_mm;
    layout->text_info = info;
    return 0;
}

/**
 * @brief Побудова розкладки з готових контурів.
 * @param page Параметри сторінки.
 * @param source_paths Вхідні контури.
 * @param layout [out] Розкладка.
 * @return 0 — успіх, інакше помилка.
 */
int drawing_build_layout_from_paths (
    const drawing_page_t *page, const geom_paths_t *source_paths, drawing_layout_t *layout) {
    if (!page || !source_paths || !layout)
        return 1;

    canvas_options_t canvas_opts = {
        .paper_w_mm = page->paper_w_mm,
        .paper_h_mm = page->paper_h_mm,
        .margin_top_mm = page->margin_top_mm,
        .margin_right_mm = page->margin_right_mm,
        .margin_bottom_mm = page->margin_bottom_mm,
        .margin_left_mm = page->margin_left_mm,
        .orientation = page->orientation,
        .font_family = NULL,
        .fit_to_frame = page->fit_to_frame ? true : false,
    };

    canvas_layout_t layout_mm;
    canvas_status_t rc = canvas_layout_document (&canvas_opts, source_paths, &layout_mm);
    if (rc == CANVAS_STATUS_INVALID_INPUT) {
        LOGE ("Некоректні параметри полотна — перевірте орієнтацію чи поля");
        return 2;
    }
    if (rc != CANVAS_STATUS_OK) {
        LOGE ("Помилка під час планування полотна (код %d)", (int)rc);
        return 1;
    }

    layout->layout = layout_mm;
    memset (&layout->text_info, 0, sizeof (layout->text_info));
    return 0;
}

void drawing_layout_dispose (drawing_layout_t *layout) {
    if (!layout)
        return;
    canvas_layout_dispose (&layout->layout);
    memset (&layout->text_info, 0, sizeof (layout->text_info));
}

/**
 * @brief Генерує SVG із розкладки.
 * @param layout Розкладка.
 * @param out [out] Буфер SVG (bytes/len).
 * @return 0 — успіх, інакше помилка.
 */
int drawing_preview_svg (const drawing_layout_t *layout, bytes_t *out) {
    if (!layout || !out)
        return 1;
    return svg_render_layout (layout, out);
}

/**
 * @brief Генерує PNG із розкладки.
 * @param layout Розкладка.
 * @param out [out] Буфер PNG (bytes/len).
 * @return 0 — успіх, інакше помилка.
 */
int drawing_preview_png (const drawing_layout_t *layout, bytes_t *out) {
    if (!layout || !out)
        return 1;
    return png_render_layout (layout, out);
}

/**
 * @brief Генерує план руху з розкладки.
 * @param layout Розкладка, що містить шляхи у мм.
 * @param limits Ліміти планування (NULL — з конфігурації).
 * @param out_blocks [out] Масив блоків руху (mallocʼиться).
 * @param out_count [out] Кількість блоків.
 * @return 0 — успіх, 1 — помилка.
 */
