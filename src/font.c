/**
 * @file font.c
 * @brief Реалізація базового контейнера SVG-шрифтів.
 */

#include "font.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdbool.h>

struct font {
    char *svg_data;
    size_t svg_len;
    char *id;
    char *family;
    font_metrics_t metrics;
    glyph_t **glyphs;
    uint32_t *glyph_codes;
    size_t glyph_count;
    bool glyphs_loaded;
};

/**
 * @brief Прочитати файл цілком у пам'ять.
 *
 * @param path     Шлях до файлу.
 * @param out_data Вихідний буфер (malloc), який згодом звільняє викликач.
 * @param out_len  Довжина прочитаних байтів (може бути NULL).
 * @return 0 при успіху; -1 при помилці I/O або пам'яті; -2 при некоректних аргументах.
 */
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

/**
 * @brief Знайти сегмент тегу в тексті SVG.
 *
 * @param data      Повний вміст SVG.
 * @param tag       Пошуковий префікс тегу (наприклад, "<font ").
 * @param out_start Повертає вказівник на початок сегмента (у data).
 * @param out_len   Довжина сегмента з урахуванням символа '>'.
 * @return 0 при успіху; 1 якщо тег не знайдено; від'ємний код при помилці.
 */
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

/**
 * @brief Виділити значення атрибуту з сегмента тегу.
 *
 * @param segment   Сегмент тегу.
 * @param seg_len   Розмір сегмента.
 * @param attr      Назва атрибуту з символом '=' (напр., "id=").
 * @param out_value Результат (malloc), належить викликачеві.
 * @return 0 при успіху; 1 якщо не знайдено; -1 при помилці, -2 при некоректних аргументах.
 */
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

/**
 * @brief Розібрати числовий атрибут або повернути fallback.
 */
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

/**
 * @brief Записати значення строкового атрибуту у поле структури.
 */
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

static int decode_utf8_char (const char *input, uint32_t *out_cp, size_t *out_len) {
    if (!input || !out_cp)
        return -2;
    const unsigned char *s = (const unsigned char *)input;
    if (s[0] == '\0')
        return -1;
    uint32_t cp = 0;
    size_t used = 0;
    if ((s[0] & 0x80) == 0) {
        cp = s[0];
        used = 1;
    } else if ((s[0] & 0xE0) == 0xC0) {
        if ((s[1] & 0xC0) != 0x80)
            return -1;
        cp = ((uint32_t)(s[0] & 0x1F) << 6) | (uint32_t)(s[1] & 0x3F);
        used = 2;
        if (cp < 0x80)
            return -1;
    } else if ((s[0] & 0xF0) == 0xE0) {
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80)
            return -1;
        cp = ((uint32_t)(s[0] & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6)
             | (uint32_t)(s[2] & 0x3F);
        used = 3;
        if (cp < 0x800)
            return -1;
    } else if ((s[0] & 0xF8) == 0xF0) {
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80)
            return -1;
        cp = ((uint32_t)(s[0] & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12)
             | ((uint32_t)(s[2] & 0x3F) << 6) | (uint32_t)(s[3] & 0x3F);
        used = 4;
        if (cp < 0x10000 || cp > 0x10FFFF)
            return -1;
    } else {
        return -1;
    }
    if (out_len)
        *out_len = used;
    *out_cp = cp;
    return 0;
}

static int parse_codepoint (const char *value, uint32_t *out_cp) {
    if (!value || !out_cp)
        return -2;
    if (value[0] == '\0')
        return -1;
    if (value[0] == '&' && value[1] == '#') {
        int base = 10;
        const char *p = value + 2;
        if (*p == 'x' || *p == 'X') {
            base = 16;
            ++p;
        }
        char *end = NULL;
        errno = 0;
        long parsed = strtol (p, &end, base);
        if (errno != 0)
            return -1;
        if (end == p)
            return -1;
        if (*end == ';')
            ++end;
        if (*end != '\0')
            return -1;
        if (parsed < 0 || parsed > 0x10FFFF)
            return -1;
        *out_cp = (uint32_t)parsed;
        return 0;
    } else {
        uint32_t cp = 0;
        if (decode_utf8_char (value, &cp, NULL) != 0)
            return -1;
        *out_cp = cp;
        return 0;
    }
}

static int append_glyph_entry (font_t *font, glyph_t *glyph, uint32_t codepoint) {
    size_t new_count = font->glyph_count + 1;
    glyph_t **glyphs = (glyph_t **)malloc (new_count * sizeof (*glyphs));
    if (!glyphs)
        return -1;
    uint32_t *codes = (uint32_t *)malloc (new_count * sizeof (*codes));
    if (!codes) {
        free (glyphs);
        return -1;
    }
    if (font->glyph_count > 0) {
        memcpy (glyphs, font->glyphs, font->glyph_count * sizeof (*glyphs));
        memcpy (codes, font->glyph_codes, font->glyph_count * sizeof (*codes));
    }
    glyphs[font->glyph_count] = glyph;
    codes[font->glyph_count] = codepoint;
    free (font->glyphs);
    free (font->glyph_codes);
    font->glyphs = glyphs;
    font->glyph_codes = codes;
    font->glyph_count = new_count;
    return 0;
}

static int ensure_glyphs_loaded (font_t *font) {
    if (font->glyphs_loaded)
        return 0;
    const char *cursor = font->svg_data;
    while (cursor && *cursor) {
        const char *glyph_tag = strstr (cursor, "<glyph");
        if (!glyph_tag)
            break;
        const char *end = strchr (glyph_tag, '>');
        if (!end)
            break;
        size_t seg_len = (size_t)(end - glyph_tag + 1);
        char *unicode_val = NULL;
        if (extract_attribute (glyph_tag, seg_len, "unicode=", &unicode_val) != 0) {
            cursor = end + 1;
            continue;
        }
        uint32_t codepoint = 0;
        if (parse_codepoint (unicode_val, &codepoint) != 0) {
            free (unicode_val);
            cursor = end + 1;
            continue;
        }
        free (unicode_val);
        bool duplicate = false;
        for (size_t i = 0; i < font->glyph_count; ++i) {
            if (font->glyph_codes[i] == codepoint) {
                duplicate = true;
                break;
            }
        }
        double adv = parse_double_with_default (
            glyph_tag, seg_len, "horiz-adv-x=", font->metrics.units_per_em);
        char *path_val = NULL;
        const char *path_data = "";
        if (extract_attribute (glyph_tag, seg_len, "d=", &path_val) == 0 && path_val)
            path_data = path_val;
        glyph_t *glyph = NULL;
        if (!duplicate && glyph_create_from_svg_path (codepoint, adv, path_data, &glyph) != 0)
            glyph = NULL;
        if (path_val)
            free (path_val);
        if (!glyph) {
            cursor = end + 1;
            continue;
        }
        if (duplicate) {
            glyph_release (glyph);
        } else {
            if (append_glyph_entry (font, glyph, codepoint) != 0) {
                glyph_release (glyph);
                return -1;
            }
        }
        cursor = end + 1;
    }
    font->glyphs_loaded = true;
    return 0;
}

/**
 * @copydoc font_load_from_file
 */
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

/**
 * @brief Скопіювати рядок у буфер користувача.
 */
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

/**
 * @copydoc font_get_id
 */
int font_get_id (const font_t *font, char *buffer, size_t buflen) {
    if (!font)
        return -1;
    return copy_string_field (font->id, buffer, buflen);
}

/**
 * @copydoc font_get_family_name
 */
int font_get_family_name (const font_t *font, char *buffer, size_t buflen) {
    if (!font)
        return -1;
    return copy_string_field (font->family, buffer, buflen);
}

/**
 * @copydoc font_get_metrics
 */
int font_get_metrics (const font_t *font, font_metrics_t *out) {
    if (!font || !out)
        return -1;
    *out = font->metrics;
    return 0;
}

/**
 * @copydoc font_find_glyph
 */
int font_find_glyph (const font_t *font, uint32_t codepoint, const glyph_t **out_glyph) {
    if (!font || !out_glyph)
        return -1;
    font_t *mutable_font = (font_t *)font;
    if (ensure_glyphs_loaded (mutable_font) != 0)
        return -1;
    for (size_t i = 0; i < mutable_font->glyph_count; ++i) {
        if (mutable_font->glyph_codes[i] == codepoint) {
            *out_glyph = mutable_font->glyphs[i];
            return 0;
        }
    }
    return 1;
}

/**
 * @copydoc font_release
 */
int font_release (font_t *font) {
    if (!font)
        return 0;
    if (font->glyphs) {
        for (size_t i = 0; i < font->glyph_count; ++i)
            glyph_release (font->glyphs[i]);
    }
    free (font->glyphs);
    free (font->glyph_codes);
    free (font->svg_data);
    free (font->id);
    free (font->family);
    free (font);
    return 0;
}
