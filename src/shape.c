/**
 * @file shape.c
 * @brief Реалізація генерації базових фігур.
 * @ingroup shape
 * @details
 * Формує полілінії для простих фігур. Кола/еліпси/заокруглення кутів
 * апроксимуються кубічними Безьє з параметром плоскості (`flatness`, мм)
 * через рекурсивне підділення.
 */

#include "shape.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/** \brief Допуск для порівняння координат при замиканні контурів, мм. */
#define SHAPE_TOL 1e-9

/**
 * @brief Тимчасовий шлях для накопичення точок перед додаванням у `geom_paths_t`.
 */
typedef struct {
    geom_point_t *pts; /**< Динамічний масив точок. */
    size_t len;        /**< Кількість точок. */
    size_t cap;        /**< Ємність масиву. */
} tmp_path_t;

/**
 * @brief Ініціалізує тимчасовий шлях із початковою ємністю.
 */
static int shape_tmp_path_init (tmp_path_t *p, size_t cap0) {
    memset (p, 0, sizeof (*p));
    if (cap0 == 0)
        return 0;
    p->pts = (geom_point_t *)malloc (cap0 * sizeof (*p->pts));
    if (!p->pts)
        return -1;
    p->cap = cap0;
    return 0;
}

/**
 * @brief Гарантує ємність щонайменше `need` точок.
 */
static int shape_tmp_path_reserve (tmp_path_t *p, size_t need) {
    if (need <= p->cap)
        return 0;
    size_t new_cap = p->cap ? p->cap : 16;
    while (new_cap < need)
        new_cap *= 2;
    geom_point_t *g = (geom_point_t *)realloc (p->pts, new_cap * sizeof (*p->pts));
    if (!g)
        return -1;
    p->pts = g;
    p->cap = new_cap;
    return 0;
}

/**
 * @brief Додає точку до тимчасового шляху.
 */
static int shape_tmp_path_push (tmp_path_t *p, double x, double y) {
    if (shape_tmp_path_reserve (p, p->len + 1) != 0)
        return -1;
    p->pts[p->len++] = (geom_point_t){ x, y };
    return 0;
}

/**
 * @brief Вивільняє памʼять тимчасового шляху.
 */
static void shape_tmp_path_free (tmp_path_t *p) {
    free (p->pts);
    memset (p, 0, sizeof (*p));
}

/**
 * @brief Відстань від точки `p` до прямої через `a`–`b`.
 */
static double shape_dist_point_to_line (geom_point_t a, geom_point_t b, geom_point_t p) {
    double vx = b.x - a.x, vy = b.y - a.y;
    double wx = p.x - a.x, wy = p.y - a.y;
    double denom = hypot (vx, vy);
    if (denom <= SHAPE_TOL)
        return hypot (wx, wy);
    double cross = fabs (vx * wy - vy * wx);
    return cross / denom;
}

/**
 * @brief Рекурсивно апроксимує кубічну криву Безьє ламаною з допуском `flatness`.
 * @param out Буфер вихідних точок (початкова точка має бути вже додана).
 * @param p0,p1,p2,p3 Опорні точки кубічної кривої.
 * @param flatness Максимально допустиме відхилення (мм).
 * @param depth Поточна глибина рекурсії (обмежена 20).
 * @return 0 — успіх; -1 — помилка памʼяті.
 */
static int shape_flatten_cubic (
    tmp_path_t *out,
    geom_point_t p0,
    geom_point_t p1,
    geom_point_t p2,
    geom_point_t p3,
    double flatness,
    int depth) {
    if (depth > 20) {

        return shape_tmp_path_push (out, p3.x, p3.y);
    }
    double d1 = shape_dist_point_to_line (p0, p3, p1);
    double d2 = shape_dist_point_to_line (p0, p3, p2);
    if (d1 <= flatness && d2 <= flatness) {

        return shape_tmp_path_push (out, p3.x, p3.y);
    }

    geom_point_t p01 = { (p0.x + p1.x) * 0.5, (p0.y + p1.y) * 0.5 };
    geom_point_t p12 = { (p1.x + p2.x) * 0.5, (p1.y + p2.y) * 0.5 };
    geom_point_t p23 = { (p2.x + p3.x) * 0.5, (p2.y + p3.y) * 0.5 };
    geom_point_t p012 = { (p01.x + p12.x) * 0.5, (p01.y + p12.y) * 0.5 };
    geom_point_t p123 = { (p12.x + p23.x) * 0.5, (p12.y + p23.y) * 0.5 };
    geom_point_t p0123 = { (p012.x + p123.x) * 0.5, (p012.y + p123.y) * 0.5 };
    if (shape_flatten_cubic (out, p0, p01, p012, p0123, flatness, depth + 1) != 0)
        return -1;
    if (shape_flatten_cubic (out, p0123, p123, p23, p3, flatness, depth + 1) != 0)
        return -1;
    return 0;
}

/**
 * @brief Додає накопичені точки як окремий шлях у `out`.
 */
static int shape_append_tmp_as_path (geom_paths_t *out, const tmp_path_t *tp) {
    if (!out || !tp || tp->len == 0)
        return 0;
    return geom_paths_push_path (out, tp->pts, tp->len);
}

/**
 * @copydoc shape_rect
 */
int shape_rect (geom_paths_t *out, double x, double y, double w, double h) {
    if (!out || !(w > 0.0) || !(h > 0.0))
        return -1;
    tmp_path_t tp;
    if (shape_tmp_path_init (&tp, 5) != 0)
        return -1;

    if (shape_tmp_path_push (&tp, x, y) != 0 || shape_tmp_path_push (&tp, x + w, y) != 0
        || shape_tmp_path_push (&tp, x + w, y + h) != 0 || shape_tmp_path_push (&tp, x, y + h) != 0
        || shape_tmp_path_push (&tp, x, y) != 0) {
        shape_tmp_path_free (&tp);
        return -1;
    }
    int rc = shape_append_tmp_as_path (out, &tp);
    shape_tmp_path_free (&tp);
    return rc;
}

/**
 * @copydoc shape_polyline
 */
int shape_polyline (geom_paths_t *out, const geom_point_t *pts, size_t len, int closed) {
    if (!out || !pts || len < 2)
        return -1;
    tmp_path_t tp;
    if (shape_tmp_path_init (&tp, len + (closed ? 1 : 0)) != 0)
        return -1;
    for (size_t i = 0; i < len; ++i) {
        if (shape_tmp_path_push (&tp, pts[i].x, pts[i].y) != 0) {
            shape_tmp_path_free (&tp);
            return -1;
        }
    }
    if (closed) {
        geom_point_t a = pts[0], b = pts[len - 1];
        if (fabs (a.x - b.x) > SHAPE_TOL || fabs (a.y - b.y) > SHAPE_TOL) {
            if (shape_tmp_path_push (&tp, a.x, a.y) != 0) {
                shape_tmp_path_free (&tp);
                return -1;
            }
        }
    }
    int rc = shape_append_tmp_as_path (out, &tp);
    shape_tmp_path_free (&tp);
    return rc;
}

/**
 * @brief Внутрішня реалізація заокругленого прямокутника.
 */
static int shape_round_rect_internal (
    geom_paths_t *out,
    double x,
    double y,
    double w,
    double h,
    double rx,
    double ry,
    double flatness) {
    tmp_path_t tp;
    if (shape_tmp_path_init (&tp, 64) != 0)
        return -1;
    const double k = 0.5522847498307936;

    double x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (rx <= 0.0 && ry <= 0.0) {
        int rc = (shape_tmp_path_push (&tp, x0, y0) || shape_tmp_path_push (&tp, x1, y0)
                  || shape_tmp_path_push (&tp, x1, y1) || shape_tmp_path_push (&tp, x0, y1)
                  || shape_tmp_path_push (&tp, x0, y0))
                     ? -1
                     : 0;
        if (rc == 0)
            rc = shape_append_tmp_as_path (out, &tp);
        shape_tmp_path_free (&tp);
        return rc;
    }
    if (rx < 0.0)
        rx = 0.0;
    if (ry < 0.0)
        ry = 0.0;
    if (rx > w * 0.5)
        rx = w * 0.5;
    if (ry > h * 0.5)
        ry = h * 0.5;

    geom_point_t p = { x0 + rx, y0 };
    if (shape_tmp_path_push (&tp, p.x, p.y) != 0) {
        shape_tmp_path_free (&tp);
        return -1;
    }

    if (shape_tmp_path_push (&tp, x1 - rx, y0) != 0) {
        shape_tmp_path_free (&tp);
        return -1;
    }

    {
        geom_point_t p0 = { x1 - rx, y0 };
        geom_point_t p1 = { p0.x + k * rx, p0.y };
        geom_point_t p3 = { x1, y0 + ry };
        geom_point_t p2 = { p3.x, p3.y - k * ry };
        if (shape_flatten_cubic (&tp, p0, p1, p2, p3, flatness, 0) != 0) {
            shape_tmp_path_free (&tp);
            return -1;
        }
    }

    if (shape_tmp_path_push (&tp, x1, y1 - ry) != 0) {
        shape_tmp_path_free (&tp);
        return -1;
    }

    {
        geom_point_t p0 = { x1, y1 - ry };
        geom_point_t p1 = { p0.x, p0.y + k * ry };
        geom_point_t p3 = { x1 - rx, y1 };
        geom_point_t p2 = { p3.x + k * rx, p3.y };
        if (shape_flatten_cubic (&tp, p0, p1, p2, p3, flatness, 0) != 0) {
            shape_tmp_path_free (&tp);
            return -1;
        }
    }

    if (shape_tmp_path_push (&tp, x0 + rx, y1) != 0) {
        shape_tmp_path_free (&tp);
        return -1;
    }

    {
        geom_point_t p0 = { x0 + rx, y1 };
        geom_point_t p1 = { p0.x - k * rx, p0.y };
        geom_point_t p3 = { x0, y1 - ry };
        geom_point_t p2 = { p3.x, p3.y + k * ry };
        if (shape_flatten_cubic (&tp, p0, p1, p2, p3, flatness, 0) != 0) {
            shape_tmp_path_free (&tp);
            return -1;
        }
    }

    if (shape_tmp_path_push (&tp, x0, y0 + ry) != 0) {
        shape_tmp_path_free (&tp);
        return -1;
    }

    {
        geom_point_t p0 = { x0, y0 + ry };
        geom_point_t p1 = { p0.x, p0.y - k * ry };
        geom_point_t p3 = { x0 + rx, y0 };
        geom_point_t p2 = { p3.x - k * rx, p3.y };
        if (shape_flatten_cubic (&tp, p0, p1, p2, p3, flatness, 0) != 0) {
            shape_tmp_path_free (&tp);
            return -1;
        }
    }

    if (shape_tmp_path_push (&tp, x0 + rx, y0) != 0) {
        shape_tmp_path_free (&tp);
        return -1;
    }

    int rc = shape_append_tmp_as_path (out, &tp);
    shape_tmp_path_free (&tp);
    return rc;
}

/**
 * @copydoc shape_round_rect
 */
int shape_round_rect (
    geom_paths_t *out,
    double x,
    double y,
    double w,
    double h,
    double rx,
    double ry,
    double flatness) {
    if (!out || !(w > 0.0) || !(h > 0.0) || flatness <= 0.0)
        return -1;
    return shape_round_rect_internal (out, x, y, w, h, rx, ry, flatness);
}

/**
 * @copydoc shape_ellipse
 */
int shape_ellipse (geom_paths_t *out, double cx, double cy, double rx, double ry, double flatness) {
    if (!out || !(rx > 0.0) || !(ry > 0.0) || flatness <= 0.0)
        return -1;
    tmp_path_t tp;
    if (shape_tmp_path_init (&tp, 64) != 0)
        return -1;
    const double k = 0.5522847498307936;

    geom_point_t p0 = { cx + rx, cy };
    if (shape_tmp_path_push (&tp, p0.x, p0.y) != 0) {
        shape_tmp_path_free (&tp);
        return -1;
    }

    {
        geom_point_t p1 = { p0.x, p0.y + k * ry };
        geom_point_t p3 = { cx, cy + ry };
        geom_point_t p2 = { p3.x + k * rx, p3.y };
        if (shape_flatten_cubic (&tp, p0, p1, p2, p3, flatness, 0) != 0)
            goto fail;
        p0 = p3;
    }

    {
        geom_point_t p1 = { p0.x - k * rx, p0.y };
        geom_point_t p3 = { cx - rx, cy };
        geom_point_t p2 = { p3.x, p3.y + k * ry };
        if (shape_flatten_cubic (&tp, p0, p1, p2, p3, flatness, 0) != 0)
            goto fail;
        p0 = p3;
    }

    {
        geom_point_t p1 = { p0.x, p0.y - k * ry };
        geom_point_t p3 = { cx, cy - ry };
        geom_point_t p2 = { p3.x - k * rx, p3.y };
        if (shape_flatten_cubic (&tp, p0, p1, p2, p3, flatness, 0) != 0)
            goto fail;
        p0 = p3;
    }

    {
        geom_point_t p1 = { p0.x + k * rx, p0.y };
        geom_point_t p3 = (geom_point_t){ cx + rx, cy };
        geom_point_t p2 = { p3.x, p3.y - k * ry };
        if (shape_flatten_cubic (&tp, p0, p1, p2, p3, flatness, 0) != 0)
            goto fail;

        if (shape_tmp_path_push (&tp, tp.pts[0].x, tp.pts[0].y) != 0)
            goto fail;
    }

    {
        int rc = shape_append_tmp_as_path (out, &tp);
        shape_tmp_path_free (&tp);
        return rc;
    }

fail:
    shape_tmp_path_free (&tp);
    return -1;
}

/**
 * @copydoc shape_circle
 */
int shape_circle (geom_paths_t *out, double cx, double cy, double r, double flatness) {
    return shape_ellipse (out, cx, cy, r, r, flatness);
}

/**
 * @copydoc shape_bezier_cubic
 */
int shape_bezier_cubic (
    geom_paths_t *out,
    geom_point_t p0,
    geom_point_t p1,
    geom_point_t p2,
    geom_point_t p3,
    double flatness) {
    if (!out || flatness <= 0.0)
        return -1;
    tmp_path_t tp;
    if (shape_tmp_path_init (&tp, 32) != 0)
        return -1;
    if (shape_tmp_path_push (&tp, p0.x, p0.y) != 0) {
        shape_tmp_path_free (&tp);
        return -1;
    }
    if (shape_flatten_cubic (&tp, p0, p1, p2, p3, flatness, 0) != 0) {
        shape_tmp_path_free (&tp);
        return -1;
    }
    int rc = shape_append_tmp_as_path (out, &tp);
    shape_tmp_path_free (&tp);
    return rc;
}

/**
 * @copydoc shape_bezier_quad
 */
int shape_bezier_quad (
    geom_paths_t *out, geom_point_t p0, geom_point_t p1, geom_point_t p2, double flatness) {

    geom_point_t c1 = { p0.x + (2.0 / 3.0) * (p1.x - p0.x), p0.y + (2.0 / 3.0) * (p1.y - p0.y) };
    geom_point_t c2 = { p2.x + (2.0 / 3.0) * (p1.x - p2.x), p2.y + (2.0 / 3.0) * (p1.y - p2.y) };
    return shape_bezier_cubic (out, p0, c1, c2, p2, flatness);
}
