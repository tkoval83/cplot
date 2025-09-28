/**
 * @file canvas.c
 * @brief Трансформація шляхів тексту у координати пристрою з урахуванням орієнтації та полів.
 */

#include "canvas.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

#ifndef M_PI_2
#define M_PI_2 (M_PI / 2.0)
#endif

/**
 * @brief Перевірити валідність вхідних параметрів полотна.
 *
 * @param opt Опції полотна (може бути NULL).
 * @return `CANVAS_STATUS_OK`, якщо параметри коректні, або код помилки в іншому разі.
 */
static canvas_status_t validate_options (const canvas_options_t *opt) {
    if (!opt)
        return CANVAS_STATUS_INVALID_INPUT;
    if (!(opt->paper_w_mm > 0.0) || !(opt->paper_h_mm > 0.0))
        return CANVAS_STATUS_INVALID_INPUT;
    if (opt->margin_top_mm < 0.0 || opt->margin_right_mm < 0.0 || opt->margin_bottom_mm < 0.0
        || opt->margin_left_mm < 0.0)
        return CANVAS_STATUS_INVALID_INPUT;
    if (opt->orientation != ORIENT_PORTRAIT && opt->orientation != ORIENT_LANDSCAPE)
        return CANVAS_STATUS_INVALID_INPUT;
    double usable_w = opt->paper_w_mm - opt->margin_left_mm - opt->margin_right_mm;
    double usable_h = opt->paper_h_mm - opt->margin_top_mm - opt->margin_bottom_mm;
    if (!(usable_w > 0.0) || !(usable_h > 0.0))
        return CANVAS_STATUS_INVALID_INPUT;
    double portrait_w = opt->paper_h_mm - opt->margin_top_mm - opt->margin_bottom_mm;
    double portrait_h = opt->paper_w_mm - opt->margin_left_mm - opt->margin_right_mm;
    if (opt->orientation == ORIENT_PORTRAIT && (!(portrait_w > 0.0) || !(portrait_h > 0.0)))
        return CANVAS_STATUS_INVALID_INPUT;
    return CANVAS_STATUS_OK;
}

/**
 * @brief Скопіювати шляхи у одиницях мм у новий буфер.
 *
 * Якщо вихідні шляхи вже в мм — виконується глибоке копіювання, інакше конвертація.
 *
 * @param src Джерело шляхів.
 * @param dst Призначення (не NULL).
 * @return Статус виконання.
 */
static canvas_status_t copy_paths_mm (const geom_paths_t *src, geom_paths_t *dst) {
    if (!src || !dst)
        return CANVAS_STATUS_INVALID_INPUT;
    int rc;
    if (src->units == GEOM_UNITS_MM) {
        rc = geom_paths_deep_copy (src, dst);
    } else {
        rc = geom_paths_convert (src, GEOM_UNITS_MM, dst);
    }
    if (rc != 0) {
        LOGE ("canvas: не вдалося скопіювати шляхи у мм");
        return CANVAS_STATUS_INTERNAL_ERROR;
    }
    geom_paths_set_units (dst, GEOM_UNITS_MM);
    return CANVAS_STATUS_OK;
}

/**
 * @brief Розрахувати розкладку текстових шляхів на сторінці.
 *
 * Обчислює переведені в мм шляхи з урахуванням полів, орієнтації та початкової точки
 * для руху пристрою. Повернені шляхи потрібно звільнити `canvas_layout_dispose()`.
 *
 * @param options      Параметри полотна (орієнтація, поля, розмір сторінки).
 * @param source_paths Вхідні шляхи (у мм або інших одиницях).
 * @param[out] out_layout Структура для результату.
 * @return Статус виконання.
 */
canvas_status_t canvas_layout_document (
    const canvas_options_t *options,
    const geom_paths_t *source_paths,
    canvas_layout_t *out_layout) {
    if (!options || !source_paths || !out_layout)
        return CANVAS_STATUS_INVALID_INPUT;

    canvas_status_t opt_rc = validate_options (options);
    if (opt_rc != CANVAS_STATUS_OK)
        return opt_rc;

    canvas_layout_t layout;
    memset (&layout, 0, sizeof (layout));
    layout.orientation = options->orientation;
    layout.paper_w_mm = options->paper_w_mm;
    layout.paper_h_mm = options->paper_h_mm;
    layout.margin_top_mm = options->margin_top_mm;
    layout.margin_right_mm = options->margin_right_mm;
    layout.margin_bottom_mm = options->margin_bottom_mm;
    layout.margin_left_mm = options->margin_left_mm;
    layout.frame_w_mm
        = (options->orientation == ORIENT_PORTRAIT)
              ? (options->paper_h_mm - options->margin_top_mm - options->margin_bottom_mm)
              : (options->paper_w_mm - options->margin_left_mm - options->margin_right_mm);
    layout.frame_h_mm
        = (options->orientation == ORIENT_PORTRAIT)
              ? (options->paper_w_mm - options->margin_left_mm - options->margin_right_mm)
              : (options->paper_h_mm - options->margin_top_mm - options->margin_bottom_mm);
    if (!(layout.frame_w_mm > 0.0) || !(layout.frame_h_mm > 0.0))
        return CANVAS_STATUS_INVALID_INPUT;

    geom_paths_t src_mm;
    canvas_status_t copy_rc = copy_paths_mm (source_paths, &src_mm);
    if (copy_rc != CANVAS_STATUS_OK)
        return copy_rc;

    bool has_points = src_mm.len > 0;
    geom_bbox_t src_bbox;
    if (has_points && geom_bbox_of_paths (&src_mm, &src_bbox) != 0)
        has_points = false;

    if (!has_points) {
        if (geom_paths_init (&layout.paths_mm, GEOM_UNITS_MM) != 0) {
            geom_paths_free (&src_mm);
            return CANVAS_STATUS_INTERNAL_ERROR;
        }
        layout.bounds_mm.min_x = 0.0;
        layout.bounds_mm.min_y = 0.0;
        layout.bounds_mm.max_x = 0.0;
        layout.bounds_mm.max_y = 0.0;
        if (layout.orientation == ORIENT_PORTRAIT) {
            layout.start_x_mm = options->paper_w_mm - options->margin_right_mm;
            layout.start_y_mm = options->margin_top_mm;
        } else {
            layout.start_x_mm = options->margin_left_mm;
            layout.start_y_mm = options->margin_top_mm;
        }
        geom_paths_free (&src_mm);
        *out_layout = layout;
        return CANVAS_STATUS_OK;
    }

    geom_paths_t normalized;
    int rc = geom_paths_translate (&src_mm, -src_bbox.min_x, -src_bbox.min_y, &normalized);
    geom_paths_free (&src_mm);
    if (rc != 0)
        return CANVAS_STATUS_INTERNAL_ERROR;

    if (layout.orientation == ORIENT_PORTRAIT) {
        geom_paths_t rotated;
        rc = geom_paths_rotate (&normalized, M_PI_2, 0.0, 0.0, &rotated);
        geom_paths_free (&normalized);
        if (rc != 0)
            return CANVAS_STATUS_INTERNAL_ERROR;

        geom_bbox_t rotated_bbox;
        if (geom_bbox_of_paths (&rotated, &rotated_bbox) != 0) {
            geom_paths_free (&rotated);
            if (geom_paths_init (&layout.paths_mm, GEOM_UNITS_MM) != 0)
                return CANVAS_STATUS_INTERNAL_ERROR;
            layout.bounds_mm.min_x = layout.bounds_mm.min_y = layout.bounds_mm.max_x
                = layout.bounds_mm.max_y = 0.0;
            layout.start_x_mm = options->paper_w_mm - options->margin_right_mm;
            layout.start_y_mm = options->margin_top_mm;
            *out_layout = layout;
            return CANVAS_STATUS_OK;
        }

        double dx = (options->paper_w_mm - options->margin_right_mm) - rotated_bbox.max_x;
        double dy = options->margin_top_mm - rotated_bbox.min_y;
        geom_paths_t placed;
        rc = geom_paths_translate (&rotated, dx, dy, &placed);
        geom_paths_free (&rotated);
        if (rc != 0)
            return CANVAS_STATUS_INTERNAL_ERROR;

        geom_bbox_of_paths (&placed, &layout.bounds_mm);
        layout.start_x_mm = layout.bounds_mm.max_x;
        layout.start_y_mm = layout.bounds_mm.min_y;
        layout.paths_mm = placed;
    } else {
        geom_bbox_t norm_bbox;
        if (geom_bbox_of_paths (&normalized, &norm_bbox) != 0) {
            geom_paths_free (&normalized);
            if (geom_paths_init (&layout.paths_mm, GEOM_UNITS_MM) != 0)
                return CANVAS_STATUS_INTERNAL_ERROR;
            layout.bounds_mm.min_x = layout.bounds_mm.min_y = layout.bounds_mm.max_x
                = layout.bounds_mm.max_y = 0.0;
            layout.start_x_mm = options->margin_left_mm;
            layout.start_y_mm = options->margin_top_mm;
            *out_layout = layout;
            return CANVAS_STATUS_OK;
        }

        double dx = options->margin_left_mm;
        double dy = options->margin_top_mm;
        geom_paths_t placed;
        rc = geom_paths_translate (&normalized, dx, dy, &placed);
        geom_paths_free (&normalized);
        if (rc != 0)
            return CANVAS_STATUS_INTERNAL_ERROR;

        geom_bbox_of_paths (&placed, &layout.bounds_mm);
        layout.start_x_mm = layout.bounds_mm.min_x;
        layout.start_y_mm = layout.bounds_mm.min_y;
        layout.paths_mm = placed;
    }

    *out_layout = layout;
    return CANVAS_STATUS_OK;
}

/**
 * @brief Звільнити ресурси, пов'язані з розкладкою.
 *
 * @param layout Структура, яку потрібно очищити (може бути NULL).
 */
void canvas_layout_dispose (canvas_layout_t *layout) {
    if (!layout)
        return;
    geom_paths_free (&layout->paths_mm);
    memset (layout, 0, sizeof (*layout));
}
