/**
 * @file glyph.c
 * @brief Реалізація контейнера для гліфів SVG.
 */

#include "glyph.h"

#include <stdlib.h>
#include <string.h>

struct glyph {
    uint32_t codepoint;
    double advance_width;
    char *path_data;
};

/**
 * @brief Скопіювати рядок у новий буфер.
 */
static int duplicate_string (const char *src, char **out_dst) {
    if (!src || !out_dst)
        return -2;
    size_t len = strlen (src);
    char *copy = (char *)malloc (len + 1);
    if (!copy)
        return -1;
    memcpy (copy, src, len + 1);
    *out_dst = copy;
    return 0;
}

/**
 * @copydoc glyph_create_from_svg_path
 */
int glyph_create_from_svg_path (
    uint32_t codepoint, double advance_width, const char *path_data, glyph_t **out_glyph) {
    if (!path_data || !out_glyph)
        return -2;
    glyph_t *glyph = (glyph_t *)calloc (1, sizeof (*glyph));
    if (!glyph)
        return -1;
    glyph->codepoint = codepoint;
    glyph->advance_width = advance_width;
    if (duplicate_string (path_data, &glyph->path_data) != 0) {
        free (glyph);
        return -1;
    }
    *out_glyph = glyph;
    return 0;
}

/**
 * @copydoc glyph_get_info
 */
int glyph_get_info (const glyph_t *glyph, glyph_info_t *out) {
    if (!glyph || !out)
        return -1;
    out->codepoint = glyph->codepoint;
    out->advance_width = glyph->advance_width;
    return 0;
}

/**
 * @copydoc glyph_get_path_data
 */
const char *glyph_get_path_data (const glyph_t *glyph) {
    if (!glyph)
        return NULL;
    return glyph->path_data;
}

/**
 * @copydoc glyph_release
 */
int glyph_release (glyph_t *glyph) {
    if (!glyph)
        return 0;
    free (glyph->path_data);
    free (glyph);
    return 0;
}
