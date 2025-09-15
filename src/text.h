/**
 * @file text.h
 * @brief Рендеринг тексту з використанням вбудованих контурних шрифтів Hershey.
 */
#ifndef TEXT_H
#define TEXT_H

#include "geom.h"
#include <stddef.h>
#include <stdint.h>

// Прапорці стилю (на майбутнє; зіставляються з найближчим варіантом Hershey)
enum {
    TEXT_STYLE_NONE = 0,
    TEXT_STYLE_BOLD = 1 << 0,
    TEXT_STYLE_ITALIC = 1 << 1,
};

typedef struct {
    char resolved_family[64]; /**< напр., "Hershey Sans medium" */
    double size_pt;           /**< запитаний кегль у пунктах */
    double line_height;       /**< у вихідних одиницях (мм/дюйм) */
    size_t rendered_glyphs;   /**< кількість відрендерених гліфів */
    size_t missing_glyphs;    /**< кодові одиниці без гліфа */
} text_render_info_t;

/**
 * Відрендерити текст у контурні шляхи, використовуючи шрифти Hershey, та за потреби повернути
 * інформацію про рендеринг.
 *
 * Контракт:
 * - Вхідний текст: UTF-8; не-ASCII байти трактуються як окремі кодові одиниці для пошуку гліфів.
 * - family: якщо NULL або порожній — використати типовий Hershey Sans Medium.
 * - size_pt: кегль у пунктах (1 pt = 1/72 дюйма). Вихідні одиниці визначає 'units'.
 * - units: GEOM_UNITS_MM або GEOM_UNITS_IN.
 * - out: ініціалізується та наповнюється шляхами; викликаючий код має викликати
 * geom_paths_free(out).
 * - info: необов'язково; заповнюється метаданими, якщо не NULL.
 *
 * @param text        Текст UTF-8 для рендерингу.
 * @param family      Необов'язково: назва родини шрифту або id; NULL — типово.
 * @param size_pt     Кегль у пунктах (1 pt = 1/72 дюйма).
 * @param style_flags Бітова маска прапорців TEXT_STYLE_*.
 * @param units       Вихідні одиниці для геометрії.
 * @param out         Вихідні шляхи; звільняються через geom_paths_free().
 * @param info        Необов'язкова інформація про рендеринг.
 * @return 0 у разі успіху; ненульове при помилці (напр., відсутні файли шрифтів).
 */
int text_render_hershey (
    const char *text,
    const char *family,
    double size_pt,
    unsigned style_flags,
    geom_units_t units,
    geom_paths_t *out,
    text_render_info_t *info);

// 005: Поведінка компонування тексту
typedef enum {
    TEXT_ALIGN_LEFT = 0,
    TEXT_ALIGN_CENTER = 1,
    TEXT_ALIGN_RIGHT = 2,
} text_align_t;

typedef struct {
    // рендеринг
    const char *family;   /**< NULL->типово */
    double size_pt;       /**< кегль */
    unsigned style_flags; /**< прапорці жирний/курсив (поки ігнорується) */
    geom_units_t units;   /**< GEOM_UNITS_MM або GEOM_UNITS_IN */
    // компонування
    double frame_width;  /**< доступна ширина для переносу, у 'units' */
    text_align_t align;  /**< left/center/right */
    int hyphenate;       /**< 0=вимкн. (типово), 1=увімкн. */
    double line_spacing; /**< множник, типово 1.2 якщо <= 0 */
} text_layout_opts_t;

typedef struct {
    size_t start_index; /**< індекс у нормалізованому буфері тексту */
    size_t length;      /**< кількість байтів у рядку */
    double width;       /**< ширина рядка у 'units' */
    double offset_x;    /**< зсув X через вирівнювання */
    double baseline_y;  /**< базова лінія Y для цього рядка у 'units' */
    int hyphenated;     /**< 1 якщо на кінці рядка вставлено дефіс */
} text_line_metrics_t;

/**
 * Відрендерити багаторядковий, за потреби з перенесенням, текст у контурні шляхи та повернути
 * метрики по рядках. Викликаючий код має звільнити lines через text_layout_free_lines() і викликати
 * geom_paths_free(out).
 *
 * @param text        Вхідний текст UTF-8 (з переведеннями рядків для жорстких розривів).
 * @param opts        Параметри компонування (не NULL).
 * @param out         Вихідні шляхи; звільняються через geom_paths_free().
 * @param lines_out   Вказівник на виділений масив метрик рядків.
 * @param lines_count Кількість елементів у lines_out.
 * @return 0 у разі успіху; ненульове при помилці.
 */
int text_layout_render (
    const char *text,
    const text_layout_opts_t *opts,
    geom_paths_t *out,
    text_line_metrics_t **lines_out,
    size_t *lines_count);

/**
 * Звільнити масив, повернений text_layout_render().
 * @param lines Вказівник, повернений через lines_out; безпечно передавати NULL.
 */
void text_layout_free_lines (text_line_metrics_t *lines);

#endif /* TEXT_H */
