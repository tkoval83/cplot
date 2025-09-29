/**
 * @file glyph.h
 * @brief Представлення та операції над гліфами Hershey.
 * @defgroup glyph Гліфи
 * @ingroup text
 * @details
 * Надає непрозорий тип гліфа, базову інформацію про нього та операції
 * створення/читання/звільнення. Джерелом контурів виступає рядок SVG-path
 * (значення атрибуту `d`).
 */
#ifndef GLYPH_H
#define GLYPH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Непрозорий тип гліфа.
 * @note Структура визначена у `glyph.c` і недоступна напряму користувачу API.
 */
typedef struct glyph glyph_t;

/**
 * Базова інформація про гліф.
 */
typedef struct glyph_info {
    uint32_t codepoint;     /**< Юнікод кодова точка гліфа. */
    double advance_width;   /**< Просування пера після гліфа (од. шрифту). */
} glyph_info_t;

/**
 * @brief Створює гліф із SVG-path даних.
 * @param codepoint Юнікод кодова точка.
 * @param advance_width Просування у одиницях шрифту (метрики шрифту).
 * @param path_data Рядок даних атрибута `d` SVG (не `NULL`).
 * @param out_glyph [out] Куди помістити вказівник на створений гліф (не `NULL`).
 * @return 0 — успіх;
 *         -1 — помилка виділення памʼяті;
 *         -2 — некоректні аргументи (`path_data==NULL` або `out_glyph==NULL`).
 */
int glyph_create_from_svg_path (
    uint32_t codepoint, double advance_width, const char *path_data, glyph_t **out_glyph);

/**
 * @brief Отримує базову інформацію про гліф.
 * @param glyph Вхідний гліф.
 * @param out [out] Структура для заповнення метаданих.
 * @return 0 — успіх; -1 — некоректні аргументи.
 */
int glyph_get_info (const glyph_t *glyph, glyph_info_t *out);

/**
 * @brief Повертає сирі SVG-path дані гліфа.
 * @param glyph Вхідний гліф.
 * @return Вказівник на внутрішній рядок даних `d` або `NULL` для `NULL` гліфа.
 * @warning Рядок належить гліфу й лишається дійсним до `glyph_release()`.
 */
const char *glyph_get_path_data (const glyph_t *glyph);

/**
 * @brief Вивільняє гліф і повʼязані ресурси.
 * @param glyph Гліф або `NULL` (у цьому разі — no-op).
 * @return 0 — завжди успіх.
 */
int glyph_release (glyph_t *glyph);

#ifdef __cplusplus
}
#endif

#endif
