/**
 * @file geom.c
 * @brief Геометричні операції над шляхами та сегментами.
 * @ingroup geom
 * @details Реалізація примітивних операцій: керування памʼяттю контейнерів,
 * трансформації (зсув/масштаб/обертання), конвертація одиниць, bbox, довжини
 * та допоміжні утиліти (порівняння, хешування).
 */

#include "geom.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/** \brief FNV‑1a 64‑біт: початкове значення (offset basis). */
#define GEOM_FNV64_OFFSET_BASIS 1469598103934665603ULL
/** \brief FNV‑1a 64‑біт: множник (prime). */
#define GEOM_FNV64_PRIME 1099511628211ULL
/** \brief Кількість мікрометрів у 1 міліметрі (для квантування). */
#define GEOM_MICROMETER_PER_MM 1000.0

/**
 * @brief Звільняє внутрішні ресурси шляху.
 * @param p Шлях; `NULL` ігнорується. Після виклику `p->pts==NULL`, `len==0`.
 */
static void geom_path_free (geom_path_t *p) {
    if (!p)
        return;
    free (p->pts);
    p->pts = NULL;
    p->len = 0;
    p->cap = 0;
}

/**
 * @copydoc geom_paths_init
 */
int geom_paths_init (geom_paths_t *ps, geom_units_t units) {
    if (!ps)
        return -1;
    ps->items = NULL;
    ps->len = 0;
    ps->cap = 0;
    ps->units = units;
    return 0;
}

/**
 * @copydoc geom_paths_free
 */
void geom_paths_free (geom_paths_t *ps) {
    if (!ps)
        return;
    for (size_t i = 0; i < ps->len; ++i)
        geom_path_free (&ps->items[i]);
    free (ps->items);
    ps->items = NULL;
    ps->len = 0;
    ps->cap = 0;
}

/**
 * @copydoc geom_path_init
 */
int geom_path_init (geom_path_t *p, size_t cap0) {
    if (!p)
        return -1;
    p->pts = NULL;
    p->len = 0;
    p->cap = 0;
    if (cap0 > 0) {
        p->pts = (geom_point_t *)malloc (cap0 * sizeof (*p->pts));
        if (!p->pts)
            return -1;
        p->cap = cap0;
    }
    return 0;
}

/**
 * @copydoc geom_path_reserve
 */
int geom_path_reserve (geom_path_t *p, size_t new_cap) {
    if (!p)
        return -1;
    if (new_cap <= p->cap)
        return 0;
    size_t cap = p->cap ? p->cap : 1;
    while (cap < new_cap)
        cap *= 2;
    geom_point_t *grown = (geom_point_t *)realloc (p->pts, cap * sizeof (*grown));
    if (!grown)
        return -1;
    p->pts = grown;
    p->cap = cap;
    return 0;
}

/**
 * @copydoc geom_paths_reserve
 */
int geom_paths_reserve (geom_paths_t *ps, size_t new_cap) {
    if (!ps)
        return -1;
    if (new_cap <= ps->cap)
        return 0;
    size_t cap = ps->cap ? ps->cap : 1;
    while (cap < new_cap)
        cap *= 2;
    geom_path_t *grown = (geom_path_t *)realloc (ps->items, cap * sizeof (*grown));
    if (!grown)
        return -1;
    if (cap > ps->cap)
        memset (grown + ps->cap, 0, (cap - ps->cap) * sizeof (*grown));
    ps->items = grown;
    ps->cap = cap;
    return 0;
}

/**
 * @copydoc geom_path_push
 */
int geom_path_push (geom_path_t *p, double x, double y) {
    if (!p)
        return -1;
    if (geom_path_reserve (p, p->len + 1) != 0)
        return -1;
    p->pts[p->len].x = x;
    p->pts[p->len].y = y;
    p->len++;
    return 0;
}

/**
 * @copydoc geom_paths_push_path
 */
int geom_paths_push_path (geom_paths_t *ps, const geom_point_t *pts, size_t len) {
    if (!ps || (len > 0 && !pts))
        return -1;
    if (geom_paths_reserve (ps, ps->len + 1) != 0)
        return -1;
    geom_path_t *dst = &ps->items[ps->len];
    if (geom_path_init (dst, len) != 0)
        return -1;
    if (len > 0) {
        memcpy (dst->pts, pts, len * sizeof (*pts));
        dst->len = len;
    }
    ps->len++;
    return 0;
}

/**
 * @copydoc geom_paths_deep_copy
 */
int geom_paths_deep_copy (const geom_paths_t *src, geom_paths_t *dst) {
    if (!src || !dst)
        return -1;
    if (geom_paths_init (dst, src->units) != 0)
        return -1;
    if (geom_paths_reserve (dst, src->len) != 0)
        return -1;
    for (size_t i = 0; i < src->len; ++i) {
        const geom_path_t *sp = &src->items[i];
        if (geom_path_init (&dst->items[i], sp->len) != 0) {
            geom_paths_free (dst);
            return -1;
        }
        if (sp->len > 0)
            memcpy (dst->items[i].pts, sp->pts, sp->len * sizeof (*sp->pts));
        dst->items[i].len = sp->len;
    }
    dst->len = src->len;
    dst->cap = src->len ? src->len : dst->cap;
    return 0;
}

/**
 * @brief Клонує контейнер шляхів (внутрішня утиліта).
 * @details Еквівалент `geom_paths_deep_copy` з копіюванням одиниць виміру.
 * @param src Джерело.
 * @param dst [out] Призначення.
 * @return 0 — успіх; -1 — помилка аргументів або виділення памʼяті.
 */
static int geom_paths_clone (const geom_paths_t *src, geom_paths_t *dst) {
    if (geom_paths_deep_copy (src, dst) != 0) {
        geom_paths_free (dst);
        return -1;
    }
    dst->units = src->units;
    return 0;
}

/**
 * @copydoc geom_paths_translate
 */
int geom_paths_translate (const geom_paths_t *a, double dx, double dy, geom_paths_t *out) {
    if (!a || !out)
        return -1;
    if (geom_paths_clone (a, out) != 0)
        return -1;
    for (size_t i = 0; i < out->len; ++i) {
        geom_path_t *p = &out->items[i];
        for (size_t j = 0; j < p->len; ++j) {
            p->pts[j].x += dx;
            p->pts[j].y += dy;
        }
    }
    return 0;
}

/**
 * @copydoc geom_paths_scale
 */
int geom_paths_scale (const geom_paths_t *a, double sx, double sy, geom_paths_t *out) {
    if (!a || !out)
        return -1;
    if (geom_paths_clone (a, out) != 0)
        return -1;
    for (size_t i = 0; i < out->len; ++i) {
        geom_path_t *p = &out->items[i];
        for (size_t j = 0; j < p->len; ++j) {
            p->pts[j].x *= sx;
            p->pts[j].y *= sy;
        }
    }
    return 0;
}

/**
 * @copydoc geom_paths_rotate
 */
int geom_paths_rotate (
    const geom_paths_t *a, double radians, double cx, double cy, geom_paths_t *out) {
    if (!a || !out)
        return -1;
    if (geom_paths_clone (a, out) != 0)
        return -1;
    double s = sin (radians);
    double c = cos (radians);
    for (size_t i = 0; i < out->len; ++i) {
        geom_path_t *p = &out->items[i];
        for (size_t j = 0; j < p->len; ++j) {
            double x = p->pts[j].x - cx;
            double y = p->pts[j].y - cy;
            double xr = x * c - y * s;
            double yr = x * s + y * c;
            p->pts[j].x = xr + cx;
            p->pts[j].y = yr + cy;
        }
    }
    return 0;
}

/**
 * @copydoc geom_bbox_of_path
 */
int geom_bbox_of_path (const geom_path_t *p, geom_bbox_t *out) {
    if (!p || !out || p->len == 0)
        return -1;
    double min_x = p->pts[0].x;
    double max_x = p->pts[0].x;
    double min_y = p->pts[0].y;
    double max_y = p->pts[0].y;
    for (size_t i = 1; i < p->len; ++i) {
        double x = p->pts[i].x;
        double y = p->pts[i].y;
        if (x < min_x)
            min_x = x;
        if (x > max_x)
            max_x = x;
        if (y < min_y)
            min_y = y;
        if (y > max_y)
            max_y = y;
    }
    out->min_x = min_x;
    out->max_x = max_x;
    out->min_y = min_y;
    out->max_y = max_y;
    return 0;
}

/**
 * @copydoc geom_bbox_of_paths
 */
int geom_bbox_of_paths (const geom_paths_t *ps, geom_bbox_t *out) {
    if (!ps || !out || ps->len == 0)
        return -1;
    bool has_point = false;
    double min_x = 0.0, max_x = 0.0, min_y = 0.0, max_y = 0.0;
    for (size_t i = 0; i < ps->len; ++i) {
        const geom_path_t *p = &ps->items[i];
        if (p->len == 0)
            continue;
        geom_bbox_t bb;
        if (geom_bbox_of_path (p, &bb) != 0)
            continue;
        if (!has_point) {
            min_x = bb.min_x;
            max_x = bb.max_x;
            min_y = bb.min_y;
            max_y = bb.max_y;
            has_point = true;
        } else {
            if (bb.min_x < min_x)
                min_x = bb.min_x;
            if (bb.max_x > max_x)
                max_x = bb.max_x;
            if (bb.min_y < min_y)
                min_y = bb.min_y;
            if (bb.max_y > max_y)
                max_y = bb.max_y;
        }
    }
    if (!has_point)
        return -1;
    out->min_x = min_x;
    out->max_x = max_x;
    out->min_y = min_y;
    out->max_y = max_y;
    return 0;
}

/**
 * @copydoc geom_path_length
 */
double geom_path_length (const geom_path_t *p) {
    if (!p || p->len < 2)
        return 0.0;
    double total = 0.0;
    for (size_t i = 1; i < p->len; ++i) {
        double dx = p->pts[i].x - p->pts[i - 1].x;
        double dy = p->pts[i].y - p->pts[i - 1].y;
        total += sqrt (dx * dx + dy * dy);
    }
    return total;
}

/**
 * @copydoc geom_paths_length
 */
double geom_paths_length (const geom_paths_t *ps) {
    if (!ps)
        return 0.0;
    double total = 0.0;
    for (size_t i = 0; i < ps->len; ++i)
        total += geom_path_length (&ps->items[i]);
    return total;
}

/**
 * @copydoc geom_paths_set_units
 */
int geom_paths_set_units (geom_paths_t *ps, geom_units_t units) {
    if (!ps)
        return -1;
    ps->units = units;
    return 0;
}

/**
 * @brief Обчислює коефіцієнт масштабування між одиницями виміру.
 * @param from Джерельні одиниці.
 * @param to Цільові одиниці.
 * @return Множник, яким слід домножити координати.
 */
static double geom_unit_scale (geom_units_t from, geom_units_t to) {
    if (from == to)
        return 1.0;
    if (from == GEOM_UNITS_MM && to == GEOM_UNITS_IN)
        return 1.0 / 25.4;
    if (from == GEOM_UNITS_IN && to == GEOM_UNITS_MM)
        return 25.4;
    return 1.0;
}

/**
 * @copydoc geom_paths_convert
 */
int geom_paths_convert (const geom_paths_t *a, geom_units_t to, geom_paths_t *out) {
    if (!a || !out)
        return -1;
    double scale = geom_unit_scale (a->units, to);
    if (geom_paths_clone (a, out) != 0)
        return -1;
    if (scale != 1.0) {
        for (size_t i = 0; i < out->len; ++i) {
            geom_path_t *p = &out->items[i];
            for (size_t j = 0; j < p->len; ++j) {
                p->pts[j].x *= scale;
                p->pts[j].y *= scale;
            }
        }
    }
    out->units = to;
    return 0;
}

/**
 * @copydoc geom_paths_normalize
 */
int geom_paths_normalize (const geom_paths_t *a, geom_paths_t *out) {
    if (!a || !out)
        return -1;
    return geom_paths_clone (a, out);
}

/**
 * @copydoc geom_point_eq
 */
int geom_point_eq (const geom_point_t *a, const geom_point_t *b, double tol) {
    if (!a || !b)
        return 0;
    return fabs (a->x - b->x) <= tol && fabs (a->y - b->y) <= tol;
}

/**
 * @copydoc geom_paths_hash_micro_mm
 * @details Використовує FNV‑1a 64‑біт із константами
 * GEOM_FNV64_OFFSET_BASIS та GEOM_FNV64_PRIME; координати квантуються
 * до мікрометрів множенням на GEOM_MICROMETER_PER_MM та округленням.
 */
uint64_t geom_paths_hash_micro_mm (const geom_paths_t *ps) {
    if (!ps)
        return 0;
    uint64_t hash = GEOM_FNV64_OFFSET_BASIS;
    const double scale = GEOM_MICROMETER_PER_MM;
    for (size_t i = 0; i < ps->len; ++i) {
        const geom_path_t *p = &ps->items[i];
        for (size_t j = 0; j < p->len; ++j) {
            uint64_t xi = (uint64_t)llabs ((long long)llround (p->pts[j].x * scale));
            uint64_t yi = (uint64_t)llabs ((long long)llround (p->pts[j].y * scale));
            hash ^= xi;
            hash *= GEOM_FNV64_PRIME;
            hash ^= yi;
            hash *= GEOM_FNV64_PRIME;
        }
    }
    return hash;
}
