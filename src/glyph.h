/**
 * @file glyph.h
 * @brief Представлення та доступ до контурних гліфів SVG.
 */
#ifndef GLYPH_H
#define GLYPH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Непрозорий дескриптор гліфа. */
typedef struct glyph glyph_t;

/**
 * Метадані гліфа для швидкого доступу без парсингу контуру.
 */
typedef struct glyph_info {
    uint32_t codepoint;   /**< Юнікод-кодова точка. */
    double advance_width; /**< Горизонтальний крок (в одиницях шрифту). */
} glyph_info_t;

/**
 * Створити гліф із SVG-атрибуту `d`.
 *
 * @param codepoint      Юнікод-значення гліфа.
 * @param advance_width  Значення horiz-adv-x у одиницях шрифту.
 * @param path_data      Рядок із SVG-командами (копіюється).
 * @param out_glyph      Вихідний гліф; не NULL.
 * @return 0 при успіху; -1 при помилці пам'яті; -2 при некоректних аргументах.
 */
int glyph_create_from_svg_path (
    uint32_t codepoint, double advance_width, const char *path_data, glyph_t **out_glyph);

/**
 * Отримати інформацію про гліф без доступу до повного контуру.
 * @param glyph   Гліф.
 * @param out     Вихідна структура для заповнення.
 * @return 0 при успіху; -1 якщо аргументи некоректні.
 */
int glyph_get_info (const glyph_t *glyph, glyph_info_t *out);

/**
 * Отримати сирий рядок контуру (SVG path `d`).
 *
 * Рядок лишається власністю гліфа та дійсний до виклику glyph_release().
 *
 * @param glyph Гліф.
 * @return Вказівник на рядок або NULL, якщо glyph==NULL.
 */
const char *glyph_get_path_data (const glyph_t *glyph);

/**
 * Звільнити гліф та пов'язані дані.
 *
 * @param glyph Гліф для звільнення (може бути NULL).
 * @return 0 при успіху.
 */
int glyph_release (glyph_t *glyph);

#ifdef __cplusplus
}
#endif

#endif /* GLYPH_H */
