/**
 * @file font.c
 * @brief Реалізація базового контейнера SVG-шрифтів.
 */

#include "font.h"

#include "log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct font {
    char *svg_data;
    size_t svg_len;
    char *id;
    char *family;
    font_metrics_t metrics;
};

static int read_entire_file (const char *path, char **out_data, size_t *out_len) {
    if (!path || !out_data)
        return -2;
    FILE *fp = fopen (path, "rb");
    if (!fp)
        return -1;
    if (fseek (fp, 0, SEEK_END) != 0) {
        fclose (fp);
        return -1;
    }
    long sz = ftell (fp);
    if (sz < 0) {
        fclose (fp);
        return -1;
    }
    if (fseek (fp, 0, SEEK_SET) != 0) {
        fclose (fp);
        return -1;
    }
    char *data = (char *)malloc ((size_t)sz + 1);
    if (!data) {
        fclose (fp);
        return -1;
    }
    size_t read = fread (data, 1, (size_t)sz, fp);
    fclose (fp);
    data[read] = '\0';
    *out_data = data;
    if (out_len)
        *out_len = read;
    return 0;
}

static int extract_attribute (const char *data, const char *attr, char **out_value) {
    if (!data || !attr || !out_value)
        return -2;
    const char *pos = strstr (data, attr);
    if (!pos)
        return 1;
    pos = strchr (pos, '"');
    if (!pos)
        return -1;
    ++pos;
    const char *end = strchr (pos, '"');
    if (!end)
        return -1;
    size_t len = (size_t)(end - pos);
    char *copy = (char *)malloc (len + 1);
    if (!copy)
        return -1;
    memcpy (copy, pos, len);
    copy[len] = '\0';
    *out_value = copy;
    return 0;
}

static double parse_double_with_default (const char *data, const char *attr, double fallback) {
    char *value = NULL;
    double result = fallback;
    int rc = extract_attribute (data, attr, &value);
    if (rc == 0 && value) {
        char *end = NULL;
        errno = 0;
        double parsed = strtod (value, &end);
        if (end != value && errno == 0)
            result = parsed;
    }
    free (value);
    return result;
}

static int set_string_field (char **field, const char *data, const char *attr) {
    if (!field)
        return -2;
    char *value = NULL;
    int rc = extract_attribute (data, attr, &value);
    if (rc == 1) {
        *field = NULL;
        return 0;
    }
    if (rc != 0)
        return -1;
    *field = value;
    return 0;
}

int font_load_from_file (const char *path, font_t **out_font) {
    if (!path || !out_font)
        return -2;
    font_t *font = (font_t *)calloc (1, sizeof (*font));
    if (!font)
        return -1;
    char *svg = NULL;
    size_t svg_len = 0;
    if (read_entire_file (path, &svg, &svg_len) != 0) {
        free (font);
        return -1;
    }
    font->svg_data = svg;
    font->svg_len = svg_len;
    if (set_string_field (&font->id, svg, "id=") != 0) {
        font_release (font);
        return -1;
    }
    if (set_string_field (&font->family, svg, "font-family=") != 0) {
        font_release (font);
        return -1;
    }
    font->metrics.units_per_em = parse_double_with_default (svg, "units-per-em=", 1000.0);
    font->metrics.ascent = parse_double_with_default (svg, "ascent=", 0.0);
    font->metrics.descent = parse_double_with_default (svg, "descent=", 0.0);
    font->metrics.cap_height = parse_double_with_default (svg, "cap-height=", 0.0);
    font->metrics.x_height = parse_double_with_default (svg, "x-height=", 0.0);
    *out_font = font;
    return 0;
}

static int copy_string_field (const char *source, char *buffer, size_t buflen) {
    if (!source)
        return 0;
    size_t len = strlen (source);
    if (!buffer || buflen == 0)
        return (int)len;
    if (len + 1 > buflen)
        len = buflen - 1;
    memcpy (buffer, source, len);
    buffer[len] = '\0';
    return (int)strlen (source);
}

int font_get_id (const font_t *font, char *buffer, size_t buflen) {
    if (!font)
        return -1;
    return copy_string_field (font->id, buffer, buflen);
}

int font_get_family_name (const font_t *font, char *buffer, size_t buflen) {
    if (!font)
        return -1;
    return copy_string_field (font->family, buffer, buflen);
}

int font_get_metrics (const font_t *font, font_metrics_t *out) {
    if (!font || !out)
        return -1;
    *out = font->metrics;
    return 0;
}

int font_find_glyph (const font_t *font, uint32_t codepoint, const glyph_t **out_glyph) {
    (void)font;
    (void)codepoint;
    (void)out_glyph;
    return 1; /* поки що шукати гліфи не реалізовано */
}

int font_release (font_t *font) {
    if (!font)
        return 0;
    free (font->svg_data);
    free (font->id);
    free (font->family);
    free (font);
    return 0;
}
