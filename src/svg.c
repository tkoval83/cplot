/**
 * @file svg.c
 * @brief Примітивний SVG-рендерер для попереднього перегляду текстових шляхів.
 */

#include "svg.h"

#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif

/**
 * @brief Забезпечити необхідну місткість рядкового буфера.
 *
 * @param buf   Буфер рядка (перевиділяється за потреби).
 * @param len   Поточна довжина записаних символів.
 * @param cap   Поточна місткість (оновлюється).
 * @param extra Кількість додаткових байтів, яку потрібно розмістити (без урахування NUL).
 * @return 0 при успіху; -1 при помилці памʼяті або некоректних аргументах.
 */
static int str_reserve (char **buf, size_t *len, size_t *cap, size_t extra) {
    size_t need = *len + extra + 1;
    if (need <= *cap)
        return 0;
    size_t new_cap = (*cap == 0) ? 256 : *cap;
    while (new_cap < need)
        new_cap *= 2;
    char *grown = (char *)realloc (*buf, new_cap);
    if (!grown)
        return -1;
    *buf = grown;
    *cap = new_cap;
    return 0;
}

/**
 * @brief Додати форматований рядок у динамічний буфер (printf-подібно).
 *
 * @param buf  Буфер рядка (перевиділяється за потреби).
 * @param len  Поточна довжина (оновлюється).
 * @param cap  Поточна місткість (оновлюється).
 * @param fmt  Формат printf.
 * @param ...  Аргументи.
 * @return 0 при успіху; -1 при помилці памʼяті або форматування.
 */
static int str_appendf (char **buf, size_t *len, size_t *cap, const char *fmt, ...) {
    va_list args;
    va_start (args, fmt);
    int needed = vsnprintf (NULL, 0, fmt, args);
    va_end (args);
    if (needed < 0)
        return -1;
    if (str_reserve (buf, len, cap, (size_t)needed) != 0)
        return -1;
    va_start (args, fmt);
    int written = vsnprintf (*buf + *len, *cap - *len, fmt, args);
    va_end (args);
    if (written < 0)
        return -1;
    *len += (size_t)written;
    return 0;
}

/**
 * @brief Згенерувати SVG-превʼю для текстової розкладки.
 *
 * @param layout Розкладка (містить шляхи у міліметрах).
 * @param[out] out Байтовий буфер SVG (malloc; звільняє викликач).
 * @return 0 при успіху; 1 при помилці памʼяті/параметрів.
 */
int svg_render_layout (const drawing_layout_t *layout, bytes_t *out) {
    if (!layout || !out)
        return 1;

    const canvas_layout_t *c = &layout->layout;
    const geom_paths_t *paths = &c->paths_mm;

    char *svg = NULL;
    size_t len = 0;
    size_t cap = 0;

    if (str_appendf (
            &svg, &len, &cap,
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.2fmm\" height=\"%.2fmm\" "
            "viewBox=\"0 0 %.4f %.4f\">\n",
            c->paper_w_mm, c->paper_h_mm, c->paper_w_mm, c->paper_h_mm)
        != 0)
        goto fail;

    if (str_appendf (
            &svg, &len, &cap,
            "  <rect x=\"0\" y=\"0\" width=\"%.4f\" height=\"%.4f\" fill=\"#ffffff\" "
            "stroke=\"#cccccc\" stroke-width=\"0.2\"/>\n",
            c->paper_w_mm, c->paper_h_mm)
        != 0)
        goto fail;

    /* Рамка робочої області не відображається у прев’ю. */

    for (size_t i = 0; i < paths->len; ++i) {
        const geom_path_t *p = &paths->items[i];
        if (!p->len || !p->pts)
            continue;
        if (str_appendf (&svg, &len, &cap, "  <path d=\"") != 0)
            goto fail;
        if (str_appendf (&svg, &len, &cap, "M %.4f %.4f", p->pts[0].x, p->pts[0].y) != 0)
            goto fail;
        for (size_t j = 1; j < p->len; ++j) {
            if (str_appendf (&svg, &len, &cap, " L %.4f %.4f", p->pts[j].x, p->pts[j].y) != 0)
                goto fail;
        }
        if (str_appendf (
                &svg, &len, &cap,
                "\" fill=\"none\" stroke=\"#000\" stroke-width=\"0.3\" stroke-linecap=\"round\" "
                "stroke-linejoin=\"round\"/>\n")
            != 0)
            goto fail;
    }

    if (str_appendf (&svg, &len, &cap, "</svg>\n") != 0)
        goto fail;

    out->bytes = (uint8_t *)svg;
    out->len = len;
    return 0;

fail:
    free (svg);
    LOGE ("Не вдалося сформувати прев’ю");
    return 1;
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
