/**
 * @file canvas.h
 * @brief Параметри полотна та рамки сторінки.
 * @defgroup canvas Полотно
 * @ingroup drawing
 */
#ifndef CPLOT_CANVAS_H
#define CPLOT_CANVAS_H

#include <stdbool.h>
#include <stddef.h>

#include "config.h"
#include "geom.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double paper_w_mm;
    double paper_h_mm;
    double margin_top_mm;
    double margin_right_mm;
    double margin_bottom_mm;
    double margin_left_mm;
    orientation_t orientation;
    const char *font_family;
    bool fit_to_frame;
} canvas_options_t;

typedef struct {
    orientation_t orientation;
    double paper_w_mm;
    double paper_h_mm;
    double margin_top_mm;
    double margin_right_mm;
    double margin_bottom_mm;
    double margin_left_mm;
    double frame_w_mm;
    double frame_h_mm;
    double start_x_mm;
    double start_y_mm;
    geom_bbox_t bounds_mm;
    geom_paths_t paths_mm;
} canvas_layout_t;

typedef enum {
    CANVAS_STATUS_OK = 0,
    CANVAS_STATUS_INVALID_INPUT = 1,
    CANVAS_STATUS_INTERNAL_ERROR = 2,
} canvas_status_t;

/**
 * @brief Будує макет полотна: нормалізує контури у мм, масштабує/обертає та розміщує в рамці.
 * @param options Параметри сторінки, полів, орієнтації та fit_to_frame.
 * @param source_paths Вхідні контури у будь-яких одиницях (конвертуються у мм).
 * @param out_layout [out] Результат із розмірами рамки, bbox і шляхами у мм.
 * @return Статус виконання.
 */
canvas_status_t canvas_layout_document (
    const canvas_options_t *options, const geom_paths_t *source_paths, canvas_layout_t *out_layout);

/**
 * @brief Звільняє ресурси, повʼязані з макетом полотна.
 * @param layout Макет, отриманий з canvas_layout_document().
 */
void canvas_layout_dispose (canvas_layout_t *layout);

#ifdef __cplusplus
}
#endif

#endif
