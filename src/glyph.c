/**
 * @file glyph.c
 * @brief Реалізація контейнера для гліфів SVG.
 */

#include "glyph.h"
#include "str.h"

#include <stdlib.h>
#include <string.h>

/**
 * @brief Приватний стан гліфа SVG.
 */
struct glyph {
    uint32_t codepoint;
    double advance_width;
    char *path_data;
};

/**
 * @brief Створити гліф із SVG-опису шляху.
 *
 * @param codepoint     Кодова точка Unicode.
 * @param advance_width Ширина кроку (у внутрішніх одиницях).
 * @param path_data     Рядок `d` із SVG.
 * @param[out] out_glyph Новий гліф (необхідно звільнити `glyph_release`).
 * @return 0 при успіху; -1 при помилці памʼяті; -2 при некоректних аргументах.
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
    if (string_duplicate (path_data, &glyph->path_data) != 0) {
        free (glyph);
        return -1;
    }
    *out_glyph = glyph;
    return 0;
}

/**
 * @brief Отримати метрики гліфа.
 *
 * @param glyph Гліф.
 * @param[out] out Структура для заповнення.
 * @return 0 при успіху; -1 якщо вказівники некоректні.
 */
int glyph_get_info (const glyph_t *glyph, glyph_info_t *out) {
    if (!glyph || !out)
        return -1;
    out->codepoint = glyph->codepoint;
    out->advance_width = glyph->advance_width;
    return 0;
}

/**
 * @brief Повернути сирий SVG-рядок `d` гліфа.
 */
const char *glyph_get_path_data (const glyph_t *glyph) {
    if (!glyph)
        return NULL;
    return glyph->path_data;
}

/**
 * @brief Звільнити гліф та повʼязані ресурси.
 */
int glyph_release (glyph_t *glyph) {
    if (!glyph)
        return 0;
    free (glyph->path_data);
    free (glyph);
    return 0;
}
