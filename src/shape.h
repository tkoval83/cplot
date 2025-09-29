/**
 * @file shape.h
 * @brief Базові геометричні фігури для тестів/демо.
 * @defgroup shape Фігури
 * @ingroup drawing
 * @details
 * Набір утиліт для генерації простих контурів у міліметрах: прямокутники,
 * заокруглені прямокутники, кола/еліпси, полілінії та криві Безьє. Криві
 * апроксимуються ламаними з контролем параметра плоскості (`flatness`, мм).
 */
#ifndef CPLOT_SHAPE_H
#define CPLOT_SHAPE_H

#include "geom.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Додає контур прямокутника з лівого верхнього кута `(x,y)`.
 * @param out [out] Накопичувач шляхів (мм).
 * @param x,y Координати верхнього лівого кута (мм).
 * @param w,h Ширина та висота (мм; >0).
 * @return 0 — успіх; -1 — помилка аргументів або памʼяті.
 */
int shape_rect (geom_paths_t *out, double x, double y, double w, double h);

/**
 * @brief Додає контур прямокутника із заокругленими кутами.
 * @param out [out] Накопичувач шляхів (мм).
 * @param x,y Координати верхнього лівого кута (мм).
 * @param w,h Габарити (мм; >0).
 * @param rx,ry Радіуси заокруглення (мм; відсічка до [0; w/2] і [0; h/2]).
 * @param flatness Максимально допустиме відхилення апроксимації (мм; >0).
 * @return 0 — успіх; -1 — помилка аргументів або памʼяті.
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
 * @brief Додає контур кола з центром `(cx,cy)` та радіусом `r`.
 * @param out [out] Накопичувач шляхів (мм).
 * @param cx,cy Центр (мм).
 * @param r Радіус (мм; >0).
 * @param flatness Максимально допустиме відхилення апроксимації (мм; >0).
 * @return 0 — успіх; -1 — помилка.
 */
int shape_circle (geom_paths_t *out, double cx, double cy, double r, double flatness);

/**
 * @brief Додає контур еліпса з центром `(cx,cy)` та півосями `rx`,`ry`.
 * @param out [out] Накопичувач шляхів (мм).
 * @param cx,cy Центр (мм).
 * @param rx,ry Півосі (мм; >0).
 * @param flatness Максимально допустиме відхилення апроксимації (мм; >0).
 * @return 0 — успіх; -1 — помилка.
 */
int shape_ellipse (geom_paths_t *out, double cx, double cy, double rx, double ry, double flatness);

/**
 * @brief Додає полілінію з масиву точок.
 * @param out [out] Накопичувач шляхів (мм).
 * @param pts Масив точок у мм.
 * @param len Кількість точок (>=2).
 * @param closed Якщо ненульове — замкнути контур (додається перша точка, якщо потрібно).
 * @return 0 — успіх; -1 — помилка.
 */
int shape_polyline (geom_paths_t *out, const geom_point_t *pts, size_t len, int closed);

/**
 * @brief Додає квадратичну криву Безьє як полілінію.
 * @param out [out] Накопичувач шляхів (мм).
 * @param p0,p1,p2 Опорні точки кривої.
 * @param flatness Максимально допустиме відхилення апроксимації (мм; >0).
 * @return 0 — успіх; -1 — помилка.
 */
int shape_bezier_quad (
    geom_paths_t *out, geom_point_t p0, geom_point_t p1, geom_point_t p2, double flatness);

/**
 * @brief Додає кубічну криву Безьє як полілінію.
 * @param out [out] Накопичувач шляхів (мм).
 * @param p0,p1,p2,p3 Опорні точки кривої.
 * @param flatness Максимально допустиме відхилення апроксимації (мм; >0).
 * @return 0 — успіх; -1 — помилка.
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

#endif
