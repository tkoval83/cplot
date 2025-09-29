/**
 * @file font.h
 * @brief Робота з шрифтами Hershey: пошук, метрики, контури гліфів.
 * @defgroup font Шрифти
 * @ingroup text
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

/**
 * @brief Непрозорий обʼєкт шрифту Hershey, завантажений із SVG.
 */
typedef struct font font_t;

/**
 * @brief Базові метрики шрифту в одиницях шрифту.
 */
typedef struct font_metrics {
    double units_per_em;  /**< Кількість одиниць шрифту на ем. */
    double ascent;        /**< Висота над базовою лінією. */
    double descent;       /**< Глибина під базовою лінією (відʼємне або додатне значення). */
    double cap_height;    /**< Висота прописних літер. */
    double x_height;      /**< Висота малих літер (x-height). */
} font_metrics_t;

/**
 * @brief Контекст рендерингу для конкретного обличчя/розміру шрифту.
 */
typedef struct font_render_context {
    font_face_t face;              /**< Обране обличчя (джерело файлу). */
    font_t *font;                  /**< Завантажений шрифт. */
    font_metrics_t metrics;        /**< Метрики обличчя. */
    double scale;                  /**< Масштаб одиниць шрифту → вибрані одиниці (мм/дюйми). */
    double line_height_units;      /**< Рекомендований інтерліньяж у одиницях шрифту. */
    double space_advance_units;    /**< Ширина пробілу (advance), од. шрифту. */
    double hyphen_advance_units;   /**< Ширина дефісу (advance), од. шрифту. */
    geom_units_t units;            /**< Вихідні одиниці (GEOM_UNITS_*) для контурів. */
} font_render_context_t;

/**
 * @brief Завантажує шрифт Hershey з файлу.
 * @param path Шлях до файлу шрифту.
 * @param out_font [out] Вихідний обʼєкт шрифту.
 * @return 0 — успіх, інакше помилка.
 */
int font_load_from_file (const char *path, font_t **out_font);

/**
 * @brief Отримує стабільний ідентифікатор шрифту (атрибут font id).
 * @param font Обʼєкт шрифту.
 * @param buffer [out] Буфер для копії ідентифікатора (може бути NULL, щоб отримати довжину).
 * @param buflen Розмір буфера.
 * @return Довжина вихідного рядка (без NUL) або -1 при помилці.
 */
int font_get_id (const font_t *font, char *buffer, size_t buflen);

/**
 * @brief Отримує відображувану назву родини шрифту (font-family).
 * @param font Обʼєкт шрифту.
 * @param buffer [out] Буфер для назви (може бути NULL, щоб дізнатись довжину).
 * @param buflen Розмір буфера.
 * @return Довжина вихідного рядка (без NUL) або -1 при помилці.
 */
int font_get_family_name (const font_t *font, char *buffer, size_t buflen);

/**
 * @brief Заповнює основні метрики шрифту.
 * @param font Обʼєкт шрифту.
 * @param out [out] Структура метрик.
 * @return 0 — успіх, -1 — помилка.
 */
int font_get_metrics (const font_t *font, font_metrics_t *out);

/**
 * @brief Знаходить гліф за кодовою точкою.
 * @param font Обʼєкт шрифту.
 * @param codepoint Юнікод кодова точка.
 * @param out_glyph [out] Вказівник на гліф.
 * @return 0 — знайдено, 1 — відсутній, -1 — помилка.
 */
int font_find_glyph (const font_t *font, uint32_t codepoint, const glyph_t **out_glyph);

/**
 * @brief Емітує контури гліфа у вихідні шляхи.
 * @param font Шрифт.
 * @param codepoint Кодова точка Unicode.
 * @param origin_x Початкова X-позиція (одиниці шрифту).
 * @param baseline_y Базова лінія Y (мм).
 * @param scale Масштаб із одиниць шрифту в цільові одиниці.
 * @param out [out] Кінцеві шляхи.
 * @param advance_units [out] Просування пера в одиницях шрифту.
 * @return 0 — успіх, 1 — гліф відсутній, -1 — помилка.
 */
int font_emit_glyph_paths (
    const font_t *font,
    uint32_t codepoint,
    double origin_x,
    double baseline_y,
    double scale,
    geom_paths_t *out,
    double *advance_units);

/**
 * @brief Готує контекст рендерингу для конкретного обличчя шрифту.
 * @param ctx [out] Контекст рендерингу.
 * @param face Опис обличчя шрифту.
 * @param size_pt Кегль у пунктах (<=0 — типове значення).
 * @param units Вихідні одиниці (мм/дюйми).
 * @return 0 — успіх, -1 — помилка.
 */
int font_render_context_init (
    font_render_context_t *ctx, const font_face_t *face, double size_pt, geom_units_t units);

/** Звільняє ресурси контексту рендерингу. */
void font_render_context_dispose (font_render_context_t *ctx);

/**
 * @brief Обличчя для fallback-рендерингу.
 */
typedef struct font_fallback_face {
    char face_id[64];             /**< Ідентифікатор обличчя. */
    font_render_context_t ctx;    /**< Контекст рендерингу для цього обличчя. */
} font_fallback_face_t;

/**
 * @brief Мапа кодових точок на індекс обличчя у fallback-списку.
 */
typedef struct font_fallback_map {
    uint32_t codepoint;  /**< Кодова точка. */
    size_t face_index;   /**< Індекс у масиві faces (або SIZE_MAX — відсутній). */
} font_fallback_map_t;

/**
 * @brief Стан механізму підстановки гліфів.
 */
typedef struct font_fallback {
    char preferred[96];           /**< Бажана родина. */
    double size_pt;               /**< Кегль у пунктах. */
    geom_units_t units;           /**< Вихідні одиниці. */
    font_fallback_face_t *faces;  /**< Динамічний список підключених облич. */
    size_t face_count;            /**< Кількість облич. */
    size_t face_cap;              /**< Ємність масиву faces. */
    font_fallback_map_t *map;     /**< Відображення кодових точок на обличчя. */
    size_t map_count;             /**< Кількість записів map. */
    size_t map_cap;               /**< Ємність масиву map. */
} font_fallback_t;

/**
 * @brief Ініціалізує механізм підстановки для відсутніх гліфів.
 * @param fallback [out] Структура стану.
 * @param preferred_family Бажана родина (може бути NULL).
 * @param size_pt Кегль у пунктах.
 * @param units Вихідні одиниці.
 * @return 0 — успіх, -1 — помилка.
 */
int font_fallback_init (
    font_fallback_t *fallback, const char *preferred_family, double size_pt, geom_units_t units);

/**
 * @brief Звільняє ресурси fallback-механізму.
 * @param fallback Структура для очищення.
 */
void font_fallback_dispose (font_fallback_t *fallback);

/**
 * @brief Емітує контури гліфа з fallback-родинами.
 * @param fallback Стан fallback.
 * @param primary_ctx Первинний контекст рендерингу.
 * @param codepoint Кодова точка Unicode.
 * @param pen_x_units Поточна X-позиція пера (одиниці шрифту).
 * @param baseline_mm Базова лінія (мм).
 * @param out [out] Кінцеві шляхи.
 * @param advance_units [out] Просування пера.
 * @param used_family [out] Назва використаної родини.
 */
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
 * @brief Перелічує кодові точки, присутні в шрифті.
 * @param font Обʼєкт шрифту.
 * @param out_codes [out] Масив кодових точок (mallocʼиться всередині; викликальник звільняє).
 * @param out_count [out] Кількість кодових точок.
 * @return 0 — успіх, -1 — помилка.
 */
int font_list_codepoints (const font_t *font, uint32_t **out_codes, size_t *out_count);

/**
 * @brief Вивільняє обʼєкт шрифту та повʼязані ресурси.
 * @param font Обʼєкт для знищення (може бути NULL).
 * @return 0 — успіх.
 */
int font_release (font_t *font);

/**
 * @brief Розвʼязує стиль/родину у готовий контекст рендерингу.
 * @param preferred_family Бажана родина (може бути NULL).
 * @param size_pt Кегль у пунктах.
 * @param units Вихідні одиниці.
 * @param style_flags Біти стилю (TEXT_STYLE_*).
 * @param out_ctx [out] Готовий контекст.
 * @return 0 — успіх, -1 — помилка.
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

#endif
