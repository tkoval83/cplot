/**
 * @file drawing.c
 * @brief Побудова розкладки тексту, превʼю та планів руху.
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
 * @brief Побудувати полілінії символів Hershey для переданого тексту.
 *
 * @param input        Вхідний рядок для відтворення.
 * @param font_family  Назва родини шрифту або NULL для типового.
 * @param out_paths    Вихідні шляхи у координатах мм (не NULL).
 * @param info         Структура з метаданими рендерингу (може бути NULL).
 * @return 0 успіх; 1 — помилка виділення пам’яті чи рендеру.
 */
static int build_text_paths (
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
 * @brief Побудувати розкладку тексту у координатах пристрою.
 *
 * @param page        Параметри сторінки та полів (не NULL).
 * @param font_family Назва родини шрифту або NULL.
 * @param input       Текст для відтворення.
 * @param layout      Вихідна структура з геометрією та метаданими (не NULL).
 * @return 0 успіх; 1 — помилка рендерингу; 2 — некоректні параметри полотна.
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
    if (build_text_paths (input, font_family, font_size_pt, frame_width_mm, &text_paths, &info)
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
 * @brief Побудувати розкладку з уже наявних шляхів (фігури, криві тощо).
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

/**
 * @brief Звільнити ресурси, повʼязані з готовою розкладкою.
 *
 * @param layout Структура для очищення (може бути NULL).
 */
void drawing_layout_dispose (drawing_layout_t *layout) {
    if (!layout)
        return;
    canvas_layout_dispose (&layout->layout);
    memset (&layout->text_info, 0, sizeof (layout->text_info));
}

/**
 * @brief Згенерувати SVG-превʼю для розкладки.
 *
 * @param layout Розкладка з геометрією (не NULL).
 * @param out    Буфер із байтами SVG (не NULL; результат потребує free()).
 * @return 0 успіх; 1 — некоректні аргументи або помилка рендерингу.
 */
int drawing_preview_svg (const drawing_layout_t *layout, bytes_t *out) {
    if (!layout || !out)
        return 1;
    return svg_render_layout (layout, out);
}

/**
 * @brief Згенерувати PNG-превʼю для розкладки.
 *
 * @param layout Розкладка з геометрією (не NULL).
 * @param out    Буфер із байтами PNG (не NULL; результат потребує free()).
 * @return 0 успіх; 1 — некоректні аргументи або помилка рендерингу.
 */
int drawing_preview_png (const drawing_layout_t *layout, bytes_t *out) {
    if (!layout || !out)
        return 1;
    return png_render_layout (layout, out);
}

/**
 * @brief Забезпечити достатню ємність масиву сегментів.
 *
 * @param segs  Вказівник на масив сегментів (не NULL).
 * @param len   Поточна кількість елементів.
 * @param cap   Поточна ємність (оновлюється).
 * @param extra Кількість додаткових елементів, що потребують місця.
 * @return 0 успіх; -1 — помилка виділення памʼяті.
 */
static int segments_reserve (planner_segment_t **segs, size_t *len, size_t *cap, size_t extra) {
    size_t need = *len + extra;
    if (need <= *cap)
        return 0;
    size_t new_cap = (*cap == 0) ? 16 : *cap;
    while (new_cap < need)
        new_cap *= 2;
    planner_segment_t *grown = (planner_segment_t *)realloc (*segs, new_cap * sizeof (*grown));
    if (!grown)
        return -1;
    *segs = grown;
    *cap = new_cap;
    return 0;
}

/**
 * @brief Додати сегмент руху до динамічного масиву.
 *
 * @param segs    Масив сегментів (розширюється за потреби).
 * @param len     Кількість уже доданих елементів.
 * @param cap     Поточна ємність масиву.
 * @param target  Цільові координати кінця сегмента (мм).
 * @param feed_mm_s Робоча швидкість сегмента (мм/с).
 * @param pen_down true → перо опущене в сегменті.
 * @return 0 успіх; -1 — помилка виділення памʼяті.
 */
static int add_segment (
    planner_segment_t **segs,
    size_t *len,
    size_t *cap,
    const double target[2],
    double feed_mm_s,
    bool pen_down) {
    if (segments_reserve (segs, len, cap, 1) != 0)
        return -1;
    (*segs)[*len].target_mm[0] = target[0];
    (*segs)[*len].target_mm[1] = target[1];
    (*segs)[*len].feed_mm_s = feed_mm_s;
    (*segs)[*len].pen_down = pen_down;
    (*len)++;
    return 0;
}

/**
 * @brief Побудувати рух AxiDraw на основі розкладки.
 *
 * @param layout      Розкладка, що містить контури у мм.
 * @param limits      Обмеження планувальника або NULL для значень за замовчуванням.
 * @param out_blocks  Вихід: масив блоків руху (виділяється, потребує free()).
 * @param out_count   Вихід: кількість блоків у масиві.
 * @return 0 успіх; 1 — помилка ініціалізації чи виділення памʼяті.
 */
int drawing_generate_motion_plan (
    const drawing_layout_t *layout,
    const planner_limits_t *limits,
    plan_block_t **out_blocks,
    size_t *out_count) {
    if (!layout || !out_blocks || !out_count)
        return 1;

    config_t cfg;
    if (config_factory_defaults (&cfg, CONFIG_DEFAULT_MODEL) != 0)
        return 1;

    if (!(cfg.speed_mm_s > 0.0) || !(cfg.accel_mm_s2 > 0.0)) {
        LOGE ("Немає активного профілю пристрою — виконайте `cplot device profile`");
        return 1;
    }

    planner_limits_t local_limits = {
        .max_speed_mm_s = cfg.speed_mm_s,
        .max_accel_mm_s2 = cfg.accel_mm_s2,
        .cornering_distance_mm = 0.5,
        .min_segment_mm = 0.1,
    };
    const planner_limits_t *use_limits = limits ? limits : &local_limits;

    const geom_paths_t *paths = &layout->layout.paths_mm;
    if (paths->len == 0) {
        *out_blocks = NULL;
        *out_count = 0;
        return 0;
    }

    planner_segment_t *segments = NULL;
    size_t seg_len = 0;
    size_t seg_cap = 0;

    bool have_current = false;
    double current[2] = { 0.0, 0.0 };
    double start_pos[2] = { 0.0, 0.0 };

    for (size_t i = 0; i < paths->len; ++i) {
        const geom_path_t *path = &paths->items[i];
        if (path->len == 0)
            continue;
        double path_start[2] = { path->pts[0].x, path->pts[0].y };
        if (!have_current) {
            start_pos[0] = path_start[0];
            start_pos[1] = path_start[1];
            current[0] = path_start[0];
            current[1] = path_start[1];
            have_current = true;
        } else if (
            fabs (current[0] - path_start[0]) > 1e-6 || fabs (current[1] - path_start[1]) > 1e-6) {
            if (add_segment (&segments, &seg_len, &seg_cap, path_start, cfg.speed_mm_s, false) != 0)
                goto fail;
            current[0] = path_start[0];
            current[1] = path_start[1];
        }

        for (size_t j = 1; j < path->len; ++j) {
            double target[2] = { path->pts[j].x, path->pts[j].y };
            if (fabs (current[0] - target[0]) <= 1e-9 && fabs (current[1] - target[1]) <= 1e-9)
                continue;
            if (add_segment (&segments, &seg_len, &seg_cap, target, cfg.speed_mm_s, true) != 0)
                goto fail;
            current[0] = target[0];
            current[1] = target[1];
        }
    }

    if (!have_current || seg_len == 0) {
        free (segments);
        *out_blocks = NULL;
        *out_count = 0;
        return 0;
    }

    plan_block_t *blocks = NULL;
    size_t block_count = 0;
    if (!planner_plan (use_limits, start_pos, segments, seg_len, &blocks, &block_count))
        goto fail;

    free (segments);
    *out_blocks = blocks;
    *out_count = block_count;
    return 0;

fail:
    free (segments);
    return 1;
}
