/**
 * @file font.h
 * @brief Інтерфейс для завантаження та доступу до SVG-шрифтів.
 */
#ifndef FONT_H
#define FONT_H

#include "glyph.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Непрозорий дескриптор шрифту. */
typedef struct font font_t;

/** Базові метрики шрифту. */
typedef struct font_metrics {
    double units_per_em; /**< Значення units-per-em. */
    double ascent;       /**< Верхній відступ. */
    double descent;      /**< Нижній відступ (від’ємний для SVG). */
    double cap_height;   /**< Висота великих літер. */
    double x_height;     /**< Висота малих літер. */
} font_metrics_t;

/**
 * Завантажити SVG-шрифт із файлу.
 *
 * @param path     Шлях до файлу SVG.
 * @param out_font Вихідний об’єкт шрифту.
 * @return 0 при успіху; -1 при помилці введення/виведення; -2 при некоректних аргументах.
 */
int font_load_from_file (const char *path, font_t **out_font);

/**
 * Отримати ідентифікатор шрифту (атрибут font id).
 *
 * @param font    Шрифт.
 * @param buffer  Буфер для запису id (може бути NULL для запиту довжини).
 * @param buflen  Розмір буфера у байтах.
 * @return Довжина id у символах (без NUL) або від’ємний код помилки.
 */
int font_get_id (const font_t *font, char *buffer, size_t buflen);

/**
 * Отримати відображувану назву шрифту (font-family).
 *
 * @param font    Шрифт.
 * @param buffer  Буфер для запису назви (може бути NULL для запиту довжини).
 * @param buflen  Розмір буфера у байтах.
 * @return Довжина назви без урахування NUL або від’ємний код помилки.
 */
int font_get_family_name (const font_t *font, char *buffer, size_t buflen);

/**
 * Отримати метрики шрифту.
 *
 * @param font Шрифт.
 * @param out  Структура для заповнення.
 * @return 0 при успіху; -1 при некоректних аргументах.
 */
int font_get_metrics (const font_t *font, font_metrics_t *out);

/**
 * Знайти гліф за кодовою точкою.
 *
 * @param font      Шрифт.
 * @param codepoint Юнікод-значення.
 * @param out_glyph Повернений гліф (посилання зберігає власник шрифту; не звільняти).
 * @return 0 при успіху; 1 якщо гліф відсутній; від’ємний код при помилці.
 */
int font_find_glyph (const font_t *font, uint32_t codepoint, const glyph_t **out_glyph);

/**
 * Звільнити ресурси шрифту.
 *
 * @param font Шрифт для звільнення (може бути NULL).
 * @return 0 при успіху.
 */
int font_release (font_t *font);

#ifdef __cplusplus
}
#endif

#endif /* FONT_H */
