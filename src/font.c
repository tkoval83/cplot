/**
 * @file font.c
 * @brief Реалізація операцій зі шрифтами Hershey.
 * @ingroup font
 */

#include "font.h"
#include "str.h"
#include "text.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdbool.h>

/**
 * @brief Гарантує ємність масиву точок для додавання елементів.
 * @param pts [in,out] Масив точок (realloc за потреби).
 * @param cap [in,out] Поточна ємність масиву.
 * @param needed Потрібна загальна кількість елементів.
 * @return 0 — успіх, -1 — помилка виділення/аргументи.
 */
static int font_ensure_point_capacity (geom_point_t **pts, size_t *cap, size_t needed) {
    if (!pts || !cap)
        return -1;
    if (*cap >= needed)
        return 0;
    size_t new_cap = (*cap == 0) ? 8 : *cap;
    while (new_cap < needed)
        new_cap *= 2;
    geom_point_t *resized = (geom_point_t *)realloc (*pts, new_cap * sizeof (*resized));
    if (!resized)
        return -1;
    *pts = resized;
    *cap = new_cap;
    return 0;
}

/**
 * @brief Додає точку у кінець масиву (із розширенням за потреби).
 * @param pts [in,out] Масив точок.
 * @param len [in,out] Поточна довжина масиву.
 * @param cap [in,out] Поточна ємність масиву.
 * @param x Координата X.
 * @param y Координата Y.
 * @return 0 — успіх, -1 — помилка.
 */
static int font_append_point (geom_point_t **pts, size_t *len, size_t *cap, double x, double y) {
    if (font_ensure_point_capacity (pts, cap, *len + 1) != 0)
        return -1;
    (*pts)[*len].x = x;
    (*pts)[*len].y = y;
    (*len)++;
    return 0;
}

/**
 * @brief Розбирає SVG path data (M/L/Z) у полілінії та додає до вихідних шляхів.
 * @param d Рядок атрибута 'd' SVG.
 * @param origin_x Початковий X у одиницях шрифту.
 * @param baseline_y Базова лінія в мм.
 * @param scale Масштаб одиниць шрифту до мм.
 * @param out [out] Вихідні шляхи (GEOM_UNITS_MM).
 * @return 0 — успіх, -1 — помилка розбору.
 */
static int font_emit_path_data (
    const char *d, double origin_x, double baseline_y, double scale, geom_paths_t *out) {
    if (!d || !out)
        return 0;

    geom_point_t *pts = NULL;
    size_t len = 0;
    size_t cap = 0;
    int rc = 0;
    bool have_path = false;

    const char *p = d;
    while (*p) {
        while (*p && isspace ((unsigned char)*p))
            ++p;
        char cmd = *p;
        if (!cmd)
            break;
        ++p;
        if (cmd != 'M' && cmd != 'L' && cmd != 'm' && cmd != 'l' && cmd != 'Z' && cmd != 'z')
            continue;
        if (cmd == 'Z' || cmd == 'z') {
            if (have_path && len >= 2) {
                if (geom_paths_push_path (out, pts, len) != 0) {
                    rc = -1;
                    break;
                }
            }
            len = 0;
            have_path = false;
            continue;
        }
        while (*p && isspace ((unsigned char)*p))
            ++p;
        char *endptr = NULL;
        double raw_x = strtod (p, &endptr);
        if (endptr == p) {
            rc = -1;
            break;
        }
        p = endptr;
        while (*p && isspace ((unsigned char)*p))
            ++p;
        double raw_y = strtod (p, &endptr);
        if (endptr == p) {
            rc = -1;
            break;
        }
        p = endptr;
        double x_mm = (origin_x + raw_x) * scale;
        double y_mm = (baseline_y - raw_y) * scale;

        if (cmd == 'M' || cmd == 'm') {
            if (have_path && len >= 2) {
                if (geom_paths_push_path (out, pts, len) != 0) {
                    rc = -1;
                    break;
                }
            }
            len = 0;
            have_path = true;
            if (font_append_point (&pts, &len, &cap, x_mm, y_mm) != 0) {
                rc = -1;
                break;
            }
        } else {
            if (!have_path) {
                have_path = true;
                len = 0;
            }
            if (font_append_point (&pts, &len, &cap, x_mm, y_mm) != 0) {
                rc = -1;
                break;
            }
        }
    }

    if (rc == 0 && have_path && len >= 2) {
        if (geom_paths_push_path (out, pts, len) != 0)
            rc = -1;
    }

    free (pts);
    return rc;
}

/**
 * @brief Внутрішнє представлення шрифту Hershey (SVG-файл + кеш гліфів).
 */
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
 * @brief Коефіцієнт переведення пунктів (pt) у задані одиниці (мм/дюйми).
 * @param units Цільові одиниці геометрії.
 * @return Кількість цільових одиниць у одному пункті.
 */
static double font_units_per_point (geom_units_t units) {
    if (units == GEOM_UNITS_MM)
        return 25.4 / 72.0;
    return 1.0 / 72.0;
}

/**
 * @brief Копіює рядок у буфер із обрізанням, повертаючи довжину джерела.
 * @param source Початковий рядок (може бути NULL).
 * @param buffer [out] Буфер призначення (може бути NULL, щоб отримати довжину).
 * @param buflen Розмір буфера призначення.
 * @return Довжина джерела (без NUL).
 */

/** @copydoc font_render_context_init */
int font_render_context_init (
    font_render_context_t *ctx, const font_face_t *face, double size_pt, geom_units_t units) {
    if (!ctx || !face)
        return -1;
    memset (ctx, 0, sizeof (*ctx));
    ctx->face = *face;
    ctx->units = units;
    if (size_pt <= 0.0)
        size_pt = 14.0;

    if (font_load_from_file (face->path, &ctx->font) != 0)
        return -1;
    if (font_get_metrics (ctx->font, &ctx->metrics) != 0 || !(ctx->metrics.units_per_em > 0.0)) {
        font_release (ctx->font);
        ctx->font = NULL;
        return -1;
    }

    double em_units = size_pt * font_units_per_point (units);
    ctx->scale = em_units / ctx->metrics.units_per_em;
    ctx->line_height_units = ctx->metrics.ascent - ctx->metrics.descent;
    if (!(ctx->line_height_units > 0.0))
        ctx->line_height_units = ctx->metrics.units_per_em;

    const glyph_t *space_glyph = NULL;
    glyph_info_t space_info;
    if (font_find_glyph (ctx->font, (uint32_t)' ', &space_glyph) == 0
        && glyph_get_info (space_glyph, &space_info) == 0) {
        ctx->space_advance_units = space_info.advance_width;
    } else {
        ctx->space_advance_units = ctx->metrics.units_per_em * 0.5;
    }

    const glyph_t *hyphen_glyph = NULL;
    glyph_info_t hyphen_info;
    if (font_find_glyph (ctx->font, (uint32_t)'-', &hyphen_glyph) == 0
        && glyph_get_info (hyphen_glyph, &hyphen_info) == 0) {
        ctx->hyphen_advance_units = hyphen_info.advance_width;
    } else {
        ctx->hyphen_advance_units = ctx->space_advance_units;
    }

    return 0;
}

/**
 * @brief Звільняє ресурси контексту рендерингу.
 * @param ctx Контекст для очищення.
 */
void font_render_context_dispose (font_render_context_t *ctx) {
    if (!ctx)
        return;
    if (ctx->font)
        font_release (ctx->font);
    memset (ctx, 0, sizeof (*ctx));
}

/**
 * @brief Кешує відповідність кодової точки обличчю у fallback-мапі.
 * @param fallback Стан fallback.
 * @param cp Кодова точка.
 * @param face_index Індекс обличчя у масиві faces або SIZE_MAX.
 * @return 0 — успіх, -1 — помилка памʼяті.
 */
static int font_fallback_store_map (font_fallback_t *fallback, uint32_t cp, size_t face_index) {
    if (fallback->map_count == fallback->map_cap) {
        size_t new_cap = (fallback->map_cap == 0) ? 16 : fallback->map_cap * 2;
        font_fallback_map_t *grown
            = (font_fallback_map_t *)realloc (fallback->map, new_cap * sizeof (*fallback->map));
        if (!grown)
            return -1;
        fallback->map = grown;
        fallback->map_cap = new_cap;
    }
    fallback->map[fallback->map_count].codepoint = cp;
    fallback->map[fallback->map_count].face_index = face_index;
    fallback->map_count++;
    return 0;
}

/** @copydoc font_fallback_init */
int font_fallback_init (
    font_fallback_t *fallback, const char *preferred_family, double size_pt, geom_units_t units) {
    if (!fallback)
        return -1;
    memset (fallback, 0, sizeof (*fallback));
    if (preferred_family && *preferred_family)
        str_string_copy (fallback->preferred, sizeof (fallback->preferred), preferred_family);
    fallback->size_pt = (size_pt > 0.0) ? size_pt : 14.0;
    fallback->units = units;
    return 0;
}

/** @copydoc font_fallback_dispose */
void font_fallback_dispose (font_fallback_t *fallback) {
    if (!fallback)
        return;
    for (size_t i = 0; i < fallback->face_count; ++i)
        font_render_context_dispose (&fallback->faces[i].ctx);
    free (fallback->faces);
    free (fallback->map);
    memset (fallback, 0, sizeof (*fallback));
}

/**
 * @brief Знаходить або створює контекст обличчя, що підтримує вказаний код символу.
 * @param fallback Стан fallback.
 * @param primary_ctx Первинний контекст (щоб не дублювати те саме обличчя).
 * @param cp Кодова точка.
 * @return Контекст або NULL, якщо не знайдено/помилка.
 */
static const font_render_context_t *font_fallback_get_context (
    font_fallback_t *fallback, const font_render_context_t *primary_ctx, uint32_t cp) {
    if (!fallback)
        return NULL;
    for (size_t i = 0; i < fallback->map_count; ++i) {
        if (fallback->map[i].codepoint == cp) {
            size_t idx = fallback->map[i].face_index;
            if (idx == SIZE_MAX || idx >= fallback->face_count)
                return NULL;
            return &fallback->faces[idx].ctx;
        }
    }

    const char *preferred = fallback->preferred[0] ? fallback->preferred : NULL;
    font_face_t face;
    if (fontreg_select_face_for_codepoints (preferred, &cp, 1, &face) != 0) {
        font_fallback_store_map (fallback, cp, SIZE_MAX);
        return NULL;
    }
    if (primary_ctx && strcmp (face.id, primary_ctx->face.id) == 0) {
        font_fallback_store_map (fallback, cp, SIZE_MAX);
        return NULL;
    }

    size_t face_index = SIZE_MAX;
    for (size_t i = 0; i < fallback->face_count; ++i) {
        if (strcmp (fallback->faces[i].face_id, face.id) == 0) {
            face_index = i;
            break;
        }
    }

    if (face_index == SIZE_MAX) {
        if (fallback->face_count == fallback->face_cap) {
            size_t new_cap = (fallback->face_cap == 0) ? 4 : fallback->face_cap * 2;
            font_fallback_face_t *grown = (font_fallback_face_t *)realloc (
                fallback->faces, new_cap * sizeof (*fallback->faces));
            if (!grown) {
                font_fallback_store_map (fallback, cp, SIZE_MAX);
                return NULL;
            }
            fallback->faces = grown;
            fallback->face_cap = new_cap;
        }
        font_fallback_face_t *slot = &fallback->faces[fallback->face_count];
        str_string_copy (slot->face_id, sizeof (slot->face_id), face.id);
        if (font_render_context_init (&slot->ctx, &face, fallback->size_pt, fallback->units) != 0) {
            font_fallback_store_map (fallback, cp, SIZE_MAX);
            return NULL;
        }
        face_index = fallback->face_count;
        fallback->face_count++;
    }

    font_fallback_store_map (fallback, cp, face_index);
    return &fallback->faces[face_index].ctx;
}

/** @copydoc font_fallback_emit */
int font_fallback_emit (
    font_fallback_t *fallback,
    const font_render_context_t *primary_ctx,
    uint32_t codepoint,
    double pen_x_units,
    double baseline_mm,
    geom_paths_t *out,
    double *advance_units,
    const char **used_family) {
    if (!fallback || !primary_ctx)
        return 1;
    if (used_family)
        *used_family = NULL;
    const font_render_context_t *fb_ctx
        = font_fallback_get_context (fallback, primary_ctx, codepoint);
    if (!fb_ctx)
        return 1;

    if (used_family && fb_ctx->face.name[0])
        *used_family = fb_ctx->face.name;

    double pen_x_mm = pen_x_units * primary_ctx->scale;
    double origin_fb_units = (fb_ctx->scale > 0.0) ? (pen_x_mm / fb_ctx->scale) : 0.0;
    double baseline_fb_units = (fb_ctx->scale > 0.0) ? (baseline_mm / fb_ctx->scale) : 0.0;

    double fb_adv_units = 0.0;
    int rc = font_emit_glyph_paths (
        fb_ctx->font, codepoint, origin_fb_units, baseline_fb_units, fb_ctx->scale, out,
        &fb_adv_units);
    if (rc == 0) {
        if (advance_units) {
            double adv_mm = fb_adv_units * fb_ctx->scale;
            *advance_units = (primary_ctx->scale > 0.0) ? (adv_mm / primary_ctx->scale) : 0.0;
        }
        return 0;
    }
    if (rc > 0) {
        font_fallback_store_map (fallback, codepoint, SIZE_MAX);
        if (used_family)
            *used_family = NULL;
        return 1;
    }
    if (used_family)
        *used_family = NULL;
    return -1;
}

/**
 * @brief Зчитує весь файл у памʼять.
 * @param path Шлях до файлу.
 * @param out_data [out] Вміст (mallocʼиться).
 * @param out_len [out] Довжина, байт (може бути NULL).
 * @return 0 — успіх; <0 — помилка.
 */
static int font_read_entire_file (const char *path, char **out_data, size_t *out_len) {
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
 * @brief Знаходить сегмент тегу <tag ...> у SVG-даних.
 * @param data Вміст SVG.
 * @param tag Послідовність початку тегу (напр., "<font ").
 * @param out_start [out] Початок сегмента.
 * @param out_len [out] Довжина сегмента.
 * @return 0 — знайдено; 1 — не знайдено; <0 — помилка.
 */
static int
font_find_tag_segment (const char *data, const char *tag, const char **out_start, size_t *out_len) {
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
    ++cursor;
    *out_start = start;
    *out_len = (size_t)(cursor - start);
    return 0;
}

/**
 * @brief Витягує значення атрибута attr="..." з фрагмента тегу.
 * @param segment Початок тегу.
 * @param seg_len Довжина сегмента.
 * @param attr Назва атрибута з '=' (напр., "font-family=").
 * @param out_value [out] Скопійоване значення (mallocʼиться) або NULL, якщо відсутній.
 * @return 0 — знайдено; 1 — відсутній; <0 — помилка.
 */
static int
font_extract_attribute (const char *segment, size_t seg_len, const char *attr, char **out_value) {
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
 * @brief Зчитує число подвійної точності з атрибута або повертає запасне значення.
 */
static double font_parse_double_with_default (
    const char *segment, size_t seg_len, const char *attr, double fallback) {
    char *value = NULL;
    double result = fallback;
    int rc = font_extract_attribute (segment, seg_len, attr, &value);
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
 * @brief Копіює значення рядкового атрибута у виділену памʼять.
 * @return 0 — успіх; 1 — відсутній атрибут; -1 — помилка.
 */
static int
font_set_string_field (char **field, const char *segment, size_t seg_len, const char *attr) {
    if (!field)
        return -2;
    char *value = NULL;
    int rc = font_extract_attribute (segment, seg_len, attr, &value);
    if (rc == 1) {
        *field = NULL;
        return 0;
    }
    if (rc != 0)
        return -1;
    *field = value;
    return 0;
}

/**
 * @brief Декодує одну UTF-8 кодову точку.
 * @param input Вхідний байтовий рядок.
 * @param out_cp [out] Декодована кодова точка.
 * @param out_len [out] Спожита кількість байтів.
 * @return 0 — успіх; -1 — недійсна послідовність; -2 — некоректні аргументи.
 */
static int font_decode_utf8_char (const char *input, uint32_t *out_cp, size_t *out_len) {
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

/**
 * @brief Парсить кодову точку з HTML entity або UTF-8 символу.
 * @param value Рядок значення (напр., "&#x2014;" або "—").
 * @param out_cp [out] Кодова точка.
 * @return 0 — успіх; -1 — помилка синтаксису; -2 — некоректні аргументи.
 */
static int font_parse_codepoint (const char *value, uint32_t *out_cp) {
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
        if (font_decode_utf8_char (value, &cp, NULL) != 0)
            return -1;
        *out_cp = cp;
        return 0;
    }
}

/**
 * @brief Додає гліф і кодову точку до таблиці шрифту.
 * @return 0 — успіх; -1 — помилка памʼяті.
 */
static int font_append_glyph_entry (font_t *font, glyph_t *glyph, uint32_t codepoint) {
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

/**
 * @brief Ліниво завантажує гліфи з SVG у внутрішні структури.
 * @param font Обʼєкт шрифту.
 * @return 0 — успіх; -1 — помилка.
 */
static int font_ensure_glyphs_loaded (font_t *font) {
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
        if (font_extract_attribute (glyph_tag, seg_len, "unicode=", &unicode_val) != 0) {
            cursor = end + 1;
            continue;
        }
        uint32_t codepoint = 0;
        if (font_parse_codepoint (unicode_val, &codepoint) != 0) {
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
        double adv = font_parse_double_with_default (
            glyph_tag, seg_len, "horiz-adv-x=", font->metrics.units_per_em);
        char *path_val = NULL;
        const char *path_data = "";
        if (font_extract_attribute (glyph_tag, seg_len, "d=", &path_val) == 0 && path_val)
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
            if (font_append_glyph_entry (font, glyph, codepoint) != 0) {
                glyph_release (glyph);
                return -1;
            }
        }
        cursor = end + 1;
    }
    font->glyphs_loaded = true;
    return 0;
}

/** @copydoc font_load_from_file */
int font_load_from_file (const char *path, font_t **out_font) {
    if (!path || !out_font)
        return -2;
    font_t *font = (font_t *)calloc (1, sizeof (*font));
    if (!font)
        return -1;
    char *svg = NULL;
    size_t svg_len = 0;
    if (font_read_entire_file (path, &svg, &svg_len) != 0) {
        free (font);
        return -1;
    }
    font->svg_data = svg;
    font->svg_len = svg_len;
    const char *font_segment = NULL;
    size_t font_seg_len = 0;
    if (font_find_tag_segment (svg, "<font ", &font_segment, &font_seg_len) != 0) {
        font_release (font);
        return -1;
    }
    if (font_set_string_field (&font->id, font_segment, font_seg_len, "id=") != 0) {
        font_release (font);
        return -1;
    }
    const char *face_segment = NULL;
    size_t face_seg_len = 0;
    if (font_find_tag_segment (svg, "<font-face", &face_segment, &face_seg_len) != 0) {
        font_release (font);
        return -1;
    }
    if (font_set_string_field (&font->family, face_segment, face_seg_len, "font-family=") != 0) {
        font_release (font);
        return -1;
    }
    font->metrics.units_per_em
        = font_parse_double_with_default (face_segment, face_seg_len, "units-per-em=", 1000.0);
    font->metrics.ascent
        = font_parse_double_with_default (face_segment, face_seg_len, "ascent=", 0.0);
    font->metrics.descent
        = font_parse_double_with_default (face_segment, face_seg_len, "descent=", 0.0);
    font->metrics.cap_height
        = font_parse_double_with_default (face_segment, face_seg_len, "cap-height=", 0.0);
    font->metrics.x_height
        = font_parse_double_with_default (face_segment, face_seg_len, "x-height=", 0.0);
    *out_font = font;
    return 0;
}

/** @copydoc font_get_id */
int font_get_id (const font_t *font, char *buffer, size_t buflen) {
    if (!font)
        return -1;
    if (buffer && buflen > 0)
        str_string_copy (buffer, buflen, font->id);
    return font->id ? (int)strlen (font->id) : 0;
}

/** @copydoc font_get_family_name */
int font_get_family_name (const font_t *font, char *buffer, size_t buflen) {
    if (!font)
        return -1;
    if (buffer && buflen > 0)
        str_string_copy (buffer, buflen, font->family);
    return font->family ? (int)strlen (font->family) : 0;
}

/** @copydoc font_get_metrics */
int font_get_metrics (const font_t *font, font_metrics_t *out) {
    if (!font || !out)
        return -1;
    *out = font->metrics;
    return 0;
}

/** @copydoc font_find_glyph */
int font_find_glyph (const font_t *font, uint32_t codepoint, const glyph_t **out_glyph) {
    if (!font || !out_glyph)
        return -1;
    font_t *mutable_font = (font_t *)font;
    if (font_ensure_glyphs_loaded (mutable_font) != 0)
        return -1;
    for (size_t i = 0; i < mutable_font->glyph_count; ++i) {
        if (mutable_font->glyph_codes[i] == codepoint) {
            *out_glyph = mutable_font->glyphs[i];
            return 0;
        }
    }
    return 1;
}

/** @copydoc font_emit_glyph_paths */
int font_emit_glyph_paths (
    const font_t *font,
    uint32_t codepoint,
    double origin_x,
    double baseline_y,
    double scale,
    geom_paths_t *out,
    double *advance_units) {
    if (!font || !out)
        return -1;

    const glyph_t *glyph = NULL;
    int rc = font_find_glyph (font, codepoint, &glyph);
    if (rc != 0 || !glyph)
        return 1;

    glyph_info_t info;
    if (glyph_get_info (glyph, &info) != 0)
        return 1;
    if (advance_units)
        *advance_units = info.advance_width;

    const char *d = glyph_get_path_data (glyph);
    if (font_emit_path_data (d, origin_x, baseline_y, scale, out) != 0)
        return -1;

    return 0;
}

/** @copydoc font_list_codepoints */
int font_list_codepoints (const font_t *font, uint32_t **out_codes, size_t *out_count) {
    if (!font || !out_codes || !out_count)
        return -1;
    font_t *mutable_font = (font_t *)font;
    if (font_ensure_glyphs_loaded (mutable_font) != 0)
        return -1;
    size_t count = mutable_font->glyph_count;
    uint32_t *codes = NULL;
    if (count > 0) {
        codes = (uint32_t *)malloc (count * sizeof (*codes));
        if (!codes)
            return -1;
        memcpy (codes, mutable_font->glyph_codes, count * sizeof (*codes));
    }
    *out_codes = codes;
    *out_count = count;
    return 0;
}

/** @copydoc font_release */
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

/** @copydoc font_style_context_resolve */
int font_style_context_resolve (
    const char *preferred_family,
    double size_pt,
    geom_units_t units,
    unsigned style_flags,
    font_render_context_t *out_ctx) {
    if (!out_ctx)
        return -1;
    const char *fam = (preferred_family && *preferred_family) ? preferred_family : NULL;
    char name[128];
    font_face_t face;

    int want_bold = (style_flags & TEXT_STYLE_BOLD) != 0;
    int want_italic = (style_flags & TEXT_STYLE_ITALIC) != 0;

    if (want_bold && want_italic) {
        if (fam) {
            snprintf (name, sizeof name, "%s Bold Italic", fam);
            if (fontreg_resolve (name, &face) == 0)
                return font_render_context_init (out_ctx, &face, size_pt, units);
        }
        if (fam) {
            snprintf (name, sizeof name, "%s Italic", fam);
            if (fontreg_resolve (name, &face) == 0)
                return font_render_context_init (out_ctx, &face, size_pt, units);
        }
        if (fam && fontreg_resolve (fam, &face) == 0)
            return font_render_context_init (out_ctx, &face, size_pt, units);
        if (fontreg_resolve (NULL, &face) == 0)
            return font_render_context_init (out_ctx, &face, size_pt, units);
        return -1;
    }
    if (want_italic) {
        if (fam) {
            snprintf (name, sizeof name, "%s Italic", fam);
            if (fontreg_resolve (name, &face) == 0)
                return font_render_context_init (out_ctx, &face, size_pt, units);
        }
        if (fam && fontreg_resolve (fam, &face) == 0)
            return font_render_context_init (out_ctx, &face, size_pt, units);
        if (fontreg_resolve (NULL, &face) == 0)
            return font_render_context_init (out_ctx, &face, size_pt, units);
        return -1;
    }
    if (want_bold) {
        if (fam) {
            snprintf (name, sizeof name, "%s Bold", fam);
            if (fontreg_resolve (name, &face) == 0)
                return font_render_context_init (out_ctx, &face, size_pt, units);
        }
        if (fam && fontreg_resolve (fam, &face) == 0)
            return font_render_context_init (out_ctx, &face, size_pt, units);
        if (fontreg_resolve (NULL, &face) == 0)
            return font_render_context_init (out_ctx, &face, size_pt, units);
        return -1;
    }
    if (fam && fontreg_resolve (fam, &face) == 0)
        return font_render_context_init (out_ctx, &face, size_pt, units);
    if (fontreg_resolve (NULL, &face) == 0)
        return font_render_context_init (out_ctx, &face, size_pt, units);
    return -1;
}
