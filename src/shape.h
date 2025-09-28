/**
 * @file shape.h
 * @brief Побудова базових фігур і кривих Безьє у вигляді ламаних шляхів.
 */
#ifndef CPLOT_SHAPE_H
#define CPLOT_SHAPE_H

#include "geom.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Додати прямокутник (зі стороною, паралельною осям) як замкнений шлях.
 * Початкова точка — (x, y) ліворуч згори.
 */
int shape_rect (geom_paths_t *out, double x, double y, double w, double h);

/**
 * Додати прямокутник із заокругленими кутами (радіуси rx, ry), замкнений шлях.
 * Якщо rx або ry <= 0, еквівалентно shape_rect().
 * Параметр flatness визначає максимально допустиме відхилення апроксимації дуг (у мм).
 */
int shape_round_rect (
    geom_paths_t *out,
    double x,
    double y,
    double w,
    double h,
    double rx,
    double ry,
    double flatness);

/**
 * Додати коло як замкнений шлях, апроксимований кубічними Безьє.
 * Параметр flatness — допуск (мм) для спрощення кривих у ламану.
 */
int shape_circle (geom_paths_t *out, double cx, double cy, double r, double flatness);

/**
 * Додати еліпс як замкнений шлях, апроксимований кубічними Безьє.
 * Параметр flatness — допуск (мм) для спрощення кривих у ламану.
 */
int shape_ellipse (geom_paths_t *out, double cx, double cy, double rx, double ry, double flatness);

/**
 * Додати ламаний шлях. Якщо closed != 0, шлях замикається (останню точку зʼєднано з першою).
 */
int shape_polyline (geom_paths_t *out, const geom_point_t *pts, size_t len, int closed);

/**
 * Додати квадратичну криву Безьє (p0, p1, p2), апроксимовану ламаною з допуском flatness (мм).
 * Створює окремий шлях, що починається у p0 і закінчується у p2.
 */
int shape_bezier_quad (
    geom_paths_t *out, geom_point_t p0, geom_point_t p1, geom_point_t p2, double flatness);

/**
 * Додати кубічну криву Безьє (p0, p1, p2, p3), апроксимовану ламаною з допуском flatness (мм).
 * Створює окремий шлях, що починається у p0 і закінчується у p3.
 */
int shape_bezier_cubic (
    geom_paths_t *out,
    geom_point_t p0,
    geom_point_t p1,
    geom_point_t p2,
    geom_point_t p3,
    double flatness);

#ifdef __cplusplus
}
#endif

#endif /* CPLOT_SHAPE_H */
