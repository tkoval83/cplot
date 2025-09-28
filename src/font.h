/**
 * @file font.h
 * @brief Інтерфейс для завантаження та доступу до SVG-шрифтів.
 */
#ifndef FONT_H
#define FONT_H

#include "fontreg.h"
#include "geom.h"
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

typedef struct font_render_context {
    font_face_t face;
    font_t *font;
    font_metrics_t metrics;
    double scale;
    double line_height_units;
    double space_advance_units;
    double hyphen_advance_units;
    geom_units_t units;
} font_render_context_t;

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
 * Відрендерити гліф у колекцію шляхів.
 *
 * @param font          Шрифт.
 * @param codepoint     Юнікод-значення гліфа.
 * @param origin_x      Початковий зсув X у одиницях шрифту.
 * @param baseline_y    Базова лінія Y у одиницях шрифту (позитивні значення вгору).
 * @param scale         Масштаб до вихідних одиниць (наприклад, мм).
 * @param out           Колекція шляхів для доповнення (не NULL).
 * @param advance_units Вихід: advance-width у вихідних одиницях шрифту (може бути NULL).
 * @return 0 успіх; 1 якщо гліф відсутній; -1 при помилці аргументів або памʼяті.
 */
int font_emit_glyph_paths (
    const font_t *font,
    uint32_t codepoint,
    double origin_x,
    double baseline_y,
    double scale,
    geom_paths_t *out,
    double *advance_units);

int font_render_context_init (
    font_render_context_t *ctx, const font_face_t *face, double size_pt, geom_units_t units);

void font_render_context_dispose (font_render_context_t *ctx);

typedef struct font_fallback_face {
    char face_id[64];
    font_render_context_t ctx;
} font_fallback_face_t;

typedef struct font_fallback_map {
    uint32_t codepoint;
    size_t face_index;
} font_fallback_map_t;

typedef struct font_fallback {
    char preferred[96];
    double size_pt;
    geom_units_t units;
    font_fallback_face_t *faces;
    size_t face_count;
    size_t face_cap;
    font_fallback_map_t *map;
    size_t map_count;
    size_t map_cap;
} font_fallback_t;

int font_fallback_init (
    font_fallback_t *fallback, const char *preferred_family, double size_pt, geom_units_t units);

void font_fallback_dispose (font_fallback_t *fallback);

int font_fallback_emit (
    font_fallback_t *fallback,
    const font_render_context_t *primary_ctx,
    uint32_t codepoint,
    double pen_x_units,
    double baseline_mm,
    geom_paths_t *out,
    double *advance_units,
    const char **used_family);

/**
 * Отримати список кодових точок, присутніх у шрифті.
 *
 * @param font       Шрифт.
 * @param out_codes  Вихід: масив кодових точок (malloc, сортування не гарантується).
 * @param out_count  Кількість елементів у масиві.
 * @return 0 успіх; -1 при помилці або некоректних аргументах.
 */
int font_list_codepoints (const font_t *font, uint32_t **out_codes, size_t *out_count);

/**
 * Звільнити ресурси шрифту.
 *
 * @param font Шрифт для звільнення (може бути NULL).
 * @return 0 при успіху.
 */
int font_release (font_t *font);

/**
 * @brief Розв'язати найкращий контекст рендерингу для бажаного стилю.
 *
 * Порядок підбору:
 *  - bold+italic → "<family> Bold Italic"; якщо немає, то Italic; якщо немає — Regular
 *  - italic      → "<family> Italic"; якщо немає — Regular
 *  - bold        → "<family> Bold";   якщо немає — Regular
 *  - none        → Regular ("<family>")
 *
 * @param preferred_family Дисплейна назва родини (напр., ctx.face.name).
 * @param size_pt          Кегль у пунктах.
 * @param units            Одиниці вихідних координат.
 * @param style_flags      Бітова маска з text.h (TEXT_STYLE_*).
 * @param out_ctx          Вихідний контекст (ініціалізується; викликач звільняє через
 *                         font_render_context_dispose()).
 * @return 0 при успіху; ненульове при помилці.
 */
int font_style_context_resolve (
    const char *preferred_family,
    double size_pt,
    geom_units_t units,
    unsigned style_flags,
    font_render_context_t *out_ctx);

#ifdef __cplusplus
}
#endif

#endif /* FONT_H */
