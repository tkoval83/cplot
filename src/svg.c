/**
 * @file svg.c
 * @brief Реалізація генерації SVG.
 * @ingroup svg
 * @details
 * Формує мінімальний SVG: заголовок із розмірами у мм, фон‑прямокутник та
 * послідовність `path` елементів, що зʼєднують точки поліліній. Буфер строїться
 * у динамічній памʼяті з автоматичним розширенням.
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
 * @brief Гарантує ємність рядкового буфера на +`extra` байтів (із `\0`).
 * @param buf [in,out] Вказівник на буфер (може бути `NULL` при старті).
 * @param len [in,out] Поточна довжина корисних даних у буфері.
 * @param cap [in,out] Поточна ємність буфера у байтах.
 * @param extra Скільки додаткових байтів потрібно записати (без урахування `\0`).
 * @return 0 — успіх; -1 — помилка виділення памʼяті.
 */
static int svg_str_reserve (char **buf, size_t *len, size_t *cap, size_t extra) {
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
 * @brief Додає до буфера рядок, сформований за `printf`‑форматом.
 * @param buf [in,out] Буфер.
 * @param len [in,out] Довжина корисних даних.
 * @param cap [in,out] Ємність буфера.
 * @param fmt Форматний рядок.
 * @return 0 — успіх; -1 — помилка виділення памʼяті/форматування.
 */
static int svg_str_appendf (char **buf, size_t *len, size_t *cap, const char *fmt, ...) {
    va_list args;
    va_start (args, fmt);
    int needed = vsnprintf (NULL, 0, fmt, args);
    va_end (args);
    if (needed < 0)
        return -1;
    if (svg_str_reserve (buf, len, cap, (size_t)needed) != 0)
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
 * @copydoc svg_render_layout
 */
int svg_render_layout (const drawing_layout_t *layout, bytes_t *out) {
    if (!layout || !out)
        return 1;

    const canvas_layout_t *c = &layout->layout;
    const geom_paths_t *paths = &c->paths_mm;

    char *svg = NULL;
    size_t len = 0;
    size_t cap = 0;

    if (svg_str_appendf (
            &svg, &len, &cap,
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%.2fmm\" height=\"%.2fmm\" "
            "viewBox=\"0 0 %.4f %.4f\">\n",
            c->paper_w_mm, c->paper_h_mm, c->paper_w_mm, c->paper_h_mm)
        != 0)
        goto fail;

    if (svg_str_appendf (
            &svg, &len, &cap,
            "  <rect x=\"0\" y=\"0\" width=\"%.4f\" height=\"%.4f\" fill=\"#ffffff\" "
            "stroke=\"#cccccc\" stroke-width=\"0.2\"/>\n",
            c->paper_w_mm, c->paper_h_mm)
        != 0)
        goto fail;

    for (size_t i = 0; i < paths->len; ++i) {
        const geom_path_t *p = &paths->items[i];
        if (!p->len || !p->pts)
            continue;
        if (svg_str_appendf (&svg, &len, &cap, "  <path d=\"") != 0)
            goto fail;
        if (svg_str_appendf (&svg, &len, &cap, "M %.4f %.4f", p->pts[0].x, p->pts[0].y) != 0)
            goto fail;
        for (size_t j = 1; j < p->len; ++j) {
            if (svg_str_appendf (&svg, &len, &cap, " L %.4f %.4f", p->pts[j].x, p->pts[j].y) != 0)
                goto fail;
        }
        if (svg_str_appendf (
                &svg, &len, &cap,
                "\" fill=\"none\" stroke=\"#000\" stroke-width=\"0.3\" stroke-linecap=\"round\" "
                "stroke-linejoin=\"round\"/>\n")
            != 0)
            goto fail;
    }

    if (svg_str_appendf (&svg, &len, &cap, "</svg>\n") != 0)
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
