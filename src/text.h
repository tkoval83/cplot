/**
 * @file text.h
 * @brief Рендеринг тексту з використанням вбудованих контурних шрифтів Hershey.
 *
 * Модуль надає високорівневі функції для перетворення рядків у контурні шляхи
 * (полілінії) з використанням шрифтів Hershey, а також механізм розкладки
 * багаторядкового тексту з переносом слів, вирівнюванням та простими
 * інлайновими стилями (жирний, курсив, підкреслення, закреслення).
 *
 * Див. також: geom.h (типи шляхів), font.h (рендеринг гліфів), fontreg.h
 * (резолвер шрифтів).
 */
#ifndef TEXT_H
#define TEXT_H

#include "geom.h"
#include <stddef.h>
#include <stdint.h>

/**
 * @defgroup text Текст і розкладка
 * @brief Рендеринг і розкладка тексту шрифтами Hershey.
 * @{ 
 */

/**
 * @name Прапорці інлайнового стилю
 * Прапорці стилю, що можуть застосовуватись до спанів або всієї строки.
 * @{ 
 */
enum {
    TEXT_STYLE_NONE = 0,          /**< Без додаткового стилю */
    TEXT_STYLE_BOLD = 1 << 0,     /**< Жирний шрифт (якщо доступний, інакше імітація) */
    TEXT_STYLE_ITALIC = 1 << 1,   /**< Курсив (якщо доступний, інакше імітація) */
    TEXT_STYLE_UNDERLINE = 1 << 2 /**< Підкреслення (лінією) */
};
/** @} */

/**
 * @brief Метадані процедури рендерингу тексту.
 */
typedef struct {
    char resolved_family[96]; /**< домінуюча родина шрифтів (найчастіше використана) */
    double size_pt;           /**< запитаний кегль у пунктах */
    double line_height;       /**< у вихідних одиницях (мм/дюйм) */
    size_t rendered_glyphs;   /**< кількість відрендерених гліфів */
    size_t missing_glyphs;    /**< кодові одиниці без гліфа */
    size_t resolved_glyphs;   /**< гліфи, що належать домінуючій родині */
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
/**
 * @brief Відрендерити однорядковий текст у шляхи.
 *
 * Виконує підбір шрифту з урахуванням присутніх кодових точок та додає
 * контури гліфів до колекції шляхів `out`. Символи, для яких відсутні гліфи
 * в обраному шрифті, обробляються через механізм фолбеків.
 *
 * @retval 0 успіх
 * @retval -1 помилка (виділення пам'яті, резолвер шрифтів, тощо)
 */
int text_render_hershey(
    const char *text,
    const char *family,
    double size_pt,
    unsigned style_flags,
    geom_units_t units,
    geom_paths_t *out,
    text_render_info_t *info);

/**
 * @brief Вирівнювання рядків у рамці розкладки.
 */
typedef enum {
    TEXT_ALIGN_LEFT = 0,   /**< по лівому краю */
    TEXT_ALIGN_CENTER = 1, /**< по центру */
    TEXT_ALIGN_RIGHT = 2,  /**< по правому краю */
} text_align_t;

/**
 * @brief Параметри розкладки багаторядкового тексту.
 *
 * Якщо деякі поля не задані або некоректні, використовуються типові значення
 * (зазначено нижче).
 */
typedef struct {
    // рендеринг
    const char *family;   /**< NULL->типово */
    double size_pt;       /**< кегль */
    unsigned style_flags; /**< прапорці жирний/курсив/підкреслення (поки ігноруються) */
    geom_units_t units;   /**< GEOM_UNITS_MM або GEOM_UNITS_IN */
    // компонування
    double frame_width;  /**< доступна ширина для переносу, у 'units' */
    text_align_t align;  /**< left/center/right */
    int hyphenate;       /**< 0=вимкн. (типово), 1=увімкн. */
    double line_spacing; /**< множник, типово 1.2 якщо <= 0 */
    int break_long_words; /**< 1: примусово ділити довгі слова, що не вміщаються у frame_width */
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
 * Опис інлайнового формату: діапазон байтів у тексті з прапорцями стилю.
 * Використовується для передачі форматування до механізму розкладки.
 */
typedef struct text_span {
    size_t start;  /**< початок у байтах у сирому UTF-8 тексті */
    size_t length; /**< довжина у байтах */
    unsigned flags;/**< бітова маска TEXT_STYLE_* */
} text_span_t;

/** Додатковий інлайновий стиль: закреслення. */
#define TEXT_STYLE_STRIKE 0x100u

/**
 * Відрендерити багаторядковий текст із перенесенням слів та простою гіпенізацією у контурні шляхи.
 *
 * Функція виконує нормалізацію пробілів (крім табів у кодових блоках), переносить слова у межах
 * `frame_width`, застосовує вирівнювання, а за потреби додає дефіс відповідно до політики з
 * `text_layout_opts_t`. Для кожного рядка повертаються метрики, синхронізовані з масивом шляхів.
 * Виклична сторона повинна звільнити `lines_out` через text_layout_free_lines() і очистити шляхи
 * функцією geom_paths_free().
 *
 * @param text        Вхідний текст UTF-8 (з переведеннями рядків для жорстких розривів).
 * @param opts        Параметри компонування (не NULL).
 * @param out         Вихідні шляхи; звільняються через geom_paths_free().
 * @param lines_out   Вказівник на виділений масив метрик рядків.
 * @param lines_count Кількість елементів у lines_out.
 * @return 0 у разі успіху; ненульове при помилці.
 */
/**
 * @brief Розкладка і рендеринг багаторядкового тексту (без спанів).
 * @copydetails text_layout_render_spans
 */
int text_layout_render(
    const char *text,
    const text_layout_opts_t *opts,
    geom_paths_t *out,
    text_line_metrics_t **lines_out,
    size_t *lines_count,
    text_render_info_t *info);

/**
 * Те саме, але із заданими інлайновими форматами (спанами).
 * Текст не змінюється; діапазони мають відповідати байтовим індексам у text.
 */
/**
 * @brief Розкладка і рендеринг багаторядкового тексту зі спанами стилів.
 *
 * Нормалізує пробіли, переносить слова в межах `frame_width`, застосовує
 * вирівнювання і, за потреби, додає дефіси. Для кожного згенерованого
 * рядка повертаються метрики, індекси і ширина. Контури додаються в `out`.
 *
 * Вказівники виходу мають бути звільнені викликаючою стороною:
 * - `geom_paths_free(out)` для шляхів;
 * - `text_layout_free_lines(*lines_out)` для масиву метрик рядків.
 *
 * @param text        Вхідний текст UTF-8.
 * @param opts        Параметри розкладки (не NULL).
 * @param spans       Масив спанів стилів або NULL.
 * @param span_count  Кількість спанів.
 * @param out         Вихідні шляхи (ініціалізуються всередині).
 * @param lines_out   Повертає масив метрик рядків (може бути NULL).
 * @param lines_count Кількість елементів у `lines_out` (може бути NULL).
 * @param info        Додаткова інформація про рендеринг (може бути NULL).
 * @retval 0 успіх
 * @retval -1 помилка (аргументи, пам'ять, резолвер шрифтів)
 */
int text_layout_render_spans(
    const char *text,
    const text_layout_opts_t *opts,
    const text_span_t *spans,
    size_t span_count,
    geom_paths_t *out,
    text_line_metrics_t **lines_out,
    size_t *lines_count,
    text_render_info_t *info);

/**
 * Звільнити масив, повернений text_layout_render().
 * @param lines Вказівник, повернений через lines_out; безпечно передавати NULL.
 */
/**
 * @brief Звільнити масив, повернений `text_layout_render()`/`text_layout_render_spans()`.
 * @param lines Вказівник, повернений через `lines_out`.
 */
void text_layout_free_lines(text_line_metrics_t *lines);

/** @} */ /* end of group: text */

#endif /* TEXT_H */
