/**
 * @file font.c
 * @brief Реалізація базового контейнера SVG-шрифтів.
 */

#include "font.h"

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
    if (read != (size_t)sz) {
        free (data);
        return -1;
    }
    data[read] = '\0';
    *out_data = data;
    if (out_len)
        *out_len = read;
    return 0;
}

static int
find_tag_segment (const char *data, const char *tag, const char **out_start, size_t *out_len) {
    if (!data || !tag || !out_start || !out_len)
        return -2;
    const char *start = strstr (data, tag);
    if (!start)
        return 1;
    const char *cursor = start;
    while (*cursor && *cursor != '>')
        cursor++;
    if (*cursor != '>')
        return -1;
    ++cursor; /* включити '>' */
    *out_start = start;
    *out_len = (size_t)(cursor - start);
    return 0;
}

static int
extract_attribute (const char *segment, size_t seg_len, const char *attr, char **out_value) {
    if (!segment || !attr || !out_value)
        return -2;
    size_t attr_len = strlen (attr);
    if (attr_len == 0)
        return -2;
    const char *seg_end = segment + seg_len;
    for (size_t i = 0; i + attr_len < seg_len; ++i) {
        if (segment[i] != attr[0])
            continue;
        if (strncmp (segment + i, attr, attr_len) != 0)
            continue;
        const char *value_start = segment + i + attr_len;
        while (value_start < seg_end && (*value_start == ' ' || *value_start == '\t'))
            ++value_start;
        if (value_start >= seg_end || *value_start != '"')
            continue;
        ++value_start;
        const char *value_end = value_start;
        while (value_end < seg_end && *value_end != '"')
            ++value_end;
        if (value_end >= seg_end)
            return -1;
        size_t len = (size_t)(value_end - value_start);
        char *copy = (char *)malloc (len + 1);
        if (!copy)
            return -1;
        memcpy (copy, value_start, len);
        copy[len] = '\0';
        *out_value = copy;
        return 0;
    }
    return 1;
}

static double
parse_double_with_default (const char *segment, size_t seg_len, const char *attr, double fallback) {
    char *value = NULL;
    double result = fallback;
    int rc = extract_attribute (segment, seg_len, attr, &value);
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

static int set_string_field (char **field, const char *segment, size_t seg_len, const char *attr) {
    if (!field)
        return -2;
    char *value = NULL;
    int rc = extract_attribute (segment, seg_len, attr, &value);
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
    const char *font_segment = NULL;
    size_t font_seg_len = 0;
    if (find_tag_segment (svg, "<font ", &font_segment, &font_seg_len) != 0) {
        font_release (font);
        return -1;
    }
    if (set_string_field (&font->id, font_segment, font_seg_len, "id=") != 0) {
        font_release (font);
        return -1;
    }
    const char *face_segment = NULL;
    size_t face_seg_len = 0;
    if (find_tag_segment (svg, "<font-face", &face_segment, &face_seg_len) != 0) {
        font_release (font);
        return -1;
    }
    if (set_string_field (&font->family, face_segment, face_seg_len, "font-family=") != 0) {
        font_release (font);
        return -1;
    }
    font->metrics.units_per_em
        = parse_double_with_default (face_segment, face_seg_len, "units-per-em=", 1000.0);
    font->metrics.ascent = parse_double_with_default (face_segment, face_seg_len, "ascent=", 0.0);
    font->metrics.descent = parse_double_with_default (face_segment, face_seg_len, "descent=", 0.0);
    font->metrics.cap_height
        = parse_double_with_default (face_segment, face_seg_len, "cap-height=", 0.0);
    font->metrics.x_height
        = parse_double_with_default (face_segment, face_seg_len, "x-height=", 0.0);
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
