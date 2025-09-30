/**
 * @file canvas.c
 * @brief Обчислення робочої рамки та перетворень полотна.
 * @ingroup canvas
 */

#include "canvas.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "log.h"

#ifndef M_PI_2
#define M_PI_2 (M_PI / 2.0)
#endif

/**
 * @brief Перевіряє коректність параметрів полотна.
 * @param opt Параметри сторінки та полів.
 * @return CANVAS_STATUS_OK або код помилки.
 */
static canvas_status_t canvas_validate_options (const canvas_options_t *opt) {
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
 * @brief Копіює або конвертує контури у міліметри.
 * @param src Вхідні контури (мм або дюйми).
 * @param dst [out] Вихідний контейнер у мм.
 * @return CANVAS_STATUS_OK або CANVAS_STATUS_INTERNAL_ERROR.
 */
static canvas_status_t canvas_copy_paths_mm (const geom_paths_t *src, geom_paths_t *dst) {
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
 * @brief Гарантує місце у масиві сегментів планувальника (realloc при потребі).
 */
static int canvas_segments_reserve (planner_segment_t **segs, size_t *len, size_t *cap, size_t extra) {
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
 * @brief Формує розкладку полотна з урахуванням орієнтації та полів.
 * @param options Параметри сторінки.
 * @param source_paths Вхідні контури.
 * @param out_layout [out] Результуючий макет.
 * @return Статус виконання.
 */
canvas_status_t canvas_layout_document (
    const canvas_options_t *options,
    const geom_paths_t *source_paths,
    canvas_layout_t *out_layout) {
    if (!options || !source_paths || !out_layout)
        return CANVAS_STATUS_INVALID_INPUT;

    LOGD (
        "canvas: fit_to_frame=%d, paper=%.2fx%.2f, margins tlbr=%.1f,%.1f,%.1f,%.1f, orient=%d",
        options->fit_to_frame ? 1 : 0, options->paper_w_mm, options->paper_h_mm,
        options->margin_top_mm, options->margin_left_mm, options->margin_bottom_mm,
        options->margin_right_mm, (int)options->orientation);

    canvas_status_t opt_rc = canvas_validate_options (options);
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
    canvas_status_t copy_rc = canvas_copy_paths_mm (source_paths, &src_mm);
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
        double frame_w = layout.frame_w_mm;
        double frame_h = layout.frame_h_mm;
        double width = rotated_bbox.max_x - rotated_bbox.min_x;
        double height = rotated_bbox.max_y - rotated_bbox.min_y;
        double scale = 1.0;
        if (options->fit_to_frame && ((width > frame_w) || (height > frame_h))) {
            double sx = frame_w / (width > 0.0 ? width : frame_w);
            double sy = frame_h / (height > 0.0 ? height : frame_h);
            scale = sx < sy ? sx : sy;
            if (!(scale > 0.0) || scale > 1.0)
                scale = 1.0;
            LOGD (
                "canvas: fit portrait scale=%.4f (w=%.2f h=%.2f frame=%.2f×%.2f)", scale, width,
                height, frame_w, frame_h);
        }
        geom_paths_t scaled = rotated;
        bool have_scaled = false;
        if (scale != 1.0) {
            if (geom_paths_scale (&rotated, scale, scale, &scaled) != 0) {
                geom_paths_free (&rotated);
                return CANVAS_STATUS_INTERNAL_ERROR;
            }
            geom_paths_free (&rotated);
            have_scaled = true;
            geom_bbox_of_paths (&scaled, &rotated_bbox);
        }
        double dx = (options->paper_w_mm - options->margin_right_mm) - rotated_bbox.max_x;
        double dy = options->margin_top_mm - rotated_bbox.min_y;
        geom_paths_t placed;
        rc = geom_paths_translate (&scaled, dx, dy, &placed);
        if (have_scaled)
            geom_paths_free (&scaled);
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
        double width = norm_bbox.max_x - norm_bbox.min_x;
        double height = norm_bbox.max_y - norm_bbox.min_y;
        double frame_w = layout.frame_w_mm;
        double frame_h = layout.frame_h_mm;
        double scale = 1.0;
        if (options->fit_to_frame && ((width > frame_w) || (height > frame_h))) {
            double sx = frame_w / (width > 0.0 ? width : frame_w);
            double sy = frame_h / (height > 0.0 ? height : frame_h);
            scale = sx < sy ? sx : sy;
            if (!(scale > 0.0) || scale > 1.0)
                scale = 1.0;
            LOGD (
                "canvas: fit landscape scale=%.4f (w=%.2f h=%.2f frame=%.2f×%.2f)", scale, width,
                height, frame_w, frame_h);
        }
        geom_paths_t scaled = normalized;
        bool have_scaled = false;
        if (scale != 1.0) {
            if (geom_paths_scale (&normalized, scale, scale, &scaled) != 0) {
                geom_paths_free (&normalized);
                return CANVAS_STATUS_INTERNAL_ERROR;
            }
            geom_paths_free (&normalized);
            have_scaled = true;
        }
        double dx = options->margin_left_mm;
        double dy = options->margin_top_mm;
        geom_paths_t placed;
        rc = geom_paths_translate (&scaled, dx, dy, &placed);
        if (have_scaled)
            geom_paths_free (&scaled);
        else
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
 * @brief Звільняє шляхи та обнуляє макет.
 * @param layout Макет для очищення.
 */
void canvas_layout_dispose (canvas_layout_t *layout) {
    if (!layout)
        return;
    geom_paths_free (&layout->paths_mm);
    memset (layout, 0, sizeof (*layout));
}

int canvas_generate_motion_plan (
    const canvas_layout_t *layout,
    const planner_limits_t *limits,
    plan_block_t **out_blocks,
    size_t *out_count) {
    if (!layout || !out_blocks || !out_count)
        return 1;

    config_t cfg;
    if (config_factory_defaults (&cfg, CONFIG_DEFAULT_MODEL) != 0)
        return 1;

    if (!(cfg.speed_mm_s > 0.0) || !(cfg.accel_mm_s2 > 0.0))
        return 1;

    planner_limits_t local_limits = {
        .max_speed_mm_s = cfg.speed_mm_s,
        .max_accel_mm_s2 = cfg.accel_mm_s2,
        .cornering_distance_mm = 0.5,
        .min_segment_mm = 0.1,
    };
    const planner_limits_t *use_limits = limits ? limits : &local_limits;

    const geom_paths_t *paths = &layout->paths_mm;
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
            if (canvas_segments_reserve (&segments, &seg_len, &seg_cap, 1) != 0)
                goto fail;
            segments[seg_len].target_mm[0] = path_start[0];
            segments[seg_len].target_mm[1] = path_start[1];
            segments[seg_len].feed_mm_s = cfg.speed_mm_s;
            segments[seg_len].pen_down = false;
            ++seg_len;
            current[0] = path_start[0];
            current[1] = path_start[1];
        }

        for (size_t j = 1; j < path->len; ++j) {
            double target[2] = { path->pts[j].x, path->pts[j].y };
            if (fabs (current[0] - target[0]) <= 1e-9 && fabs (current[1] - target[1]) <= 1e-9)
                continue;
            if (canvas_segments_reserve (&segments, &seg_len, &seg_cap, 1) != 0)
                goto fail;
            segments[seg_len].target_mm[0] = target[0];
            segments[seg_len].target_mm[1] = target[1];
            segments[seg_len].feed_mm_s = cfg.speed_mm_s;
            segments[seg_len].pen_down = true;
            ++seg_len;
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
