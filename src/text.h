/**
 * @file text.h
 * @brief Рендеринг тексту шрифтами Hershey та розкладка рядків.
 * @defgroup text Текст
 * @details
 * Модуль забезпечує рендеринг тексту у контури Hershey та верстку багаторядкових
 * блоків у заданій рамці. Підтримуються базові стилі (напівжирний, курсив,
 * підкреслення/закреслення), вирівнювання та примітивне перенесення слів.
 */
#ifndef TEXT_H
#define TEXT_H

#include "geom.h"
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Бітові прапорці стилів тексту.
 */
enum {
    TEXT_STYLE_NONE = 0,          /**< Без стилю. */
    TEXT_STYLE_BOLD = 1 << 0,     /**< Напівжирний. */
    TEXT_STYLE_ITALIC = 1 << 1,   /**< Курсив. */
    TEXT_STYLE_UNDERLINE = 1 << 2 /**< Підкреслення. */
};

/**
 * @brief Узагальнена інформація про процес рендерингу тексту.
 */
typedef struct {
    char resolved_family[96]; /**< Фактично використана родина шрифту. */
    double size_pt;           /**< Кегль у пунктах. */
    double line_height;       /**< Висота рядка у вихідних одиницях (мм/дюйми). */
    size_t rendered_glyphs;   /**< Кількість відрендерених гліфів. */
    size_t missing_glyphs;    /**< Кількість відсутніх гліфів (замінених). */
    size_t resolved_glyphs;   /**< Кількість гліфів, що належать resolved_family. */
} text_render_info_t;

/**
 * @brief Рендерить контури тексту у вказаних одиницях (мм/дюйми).
 * @param text Текст (UTF‑8). `NULL` розглядається як порожній рядок.
 * @param family Бажана родина шрифту (може бути `NULL` — буде обрано за замовчуванням).
 * @param size_pt Кегль у пунктах (>0; інакше використовується 14pt).
 * @param style_flags Бітові прапорці стилів (`TEXT_STYLE_*`). Наразі застосовується для декорацій.
 * @param units Одиниці геометрії вихідних контурів.
 * @param out [out] Контейнери шляхів; ініціалізуються всередині.
 * @param info [out] За бажанням — агреговані метрики рендерингу.
 * @return 0 — успіх; -1 — помилка аргументів/виділення/шрифтів.
 */
int text_render_hershey (
    const char *text,
    const char *family,
    double size_pt,
    unsigned style_flags,
    geom_units_t units,
    geom_paths_t *out,
    text_render_info_t *info);

/**
 * @brief Вирівнювання рядків відносно рамки.
 */
typedef enum {
    TEXT_ALIGN_LEFT = 0,   /**< По лівому краю. */
    TEXT_ALIGN_CENTER = 1, /**< По центру. */
    TEXT_ALIGN_RIGHT = 2,  /**< По правому краю. */
} text_align_t;

/**
 * @brief Опції верстки текстового блоку.
 */
typedef struct {
    const char *family;   /**< Базова родина шрифту. */
    double size_pt;       /**< Кегль у пунктах. */
    unsigned style_flags; /**< Базові стилі (`TEXT_STYLE_*`). */
    geom_units_t units;   /**< Одиниці вихідних контурів. */

    double frame_width;   /**< Ширина рамки для переносу рядків (у відповідних одиницях). */
    text_align_t align;   /**< Вирівнювання рядків. */
    int hyphenate;        /**< Дозволити перенос по дефісу (1/0). */
    double line_spacing;  /**< Множник міжрядкового інтервалу (1.0..). */
    int break_long_words; /**< Примусово ламати надто довгі слова (1/0). */
} text_layout_opts_t;

/**
 * @brief Метрики одного рядка після верстки.
 */
typedef struct {
    size_t start_index; /**< Початковий індекс у вхідному UTF‑8 рядку. */
    size_t length;      /**< Довжина підрядка у байтах. */
    double width;       /**< Ширина рядка у вихідних одиницях. */
    double offset_x;    /**< Зсув X (для вирівнювання). */
    double baseline_y;  /**< Y базової лінії. */
    int hyphenated;     /**< 1, якщо рядок завершено переносом. */
} text_line_metrics_t;

/**
 * @brief Діапазон символів із окремим стилем/опціями.
 */
typedef struct text_span {
    size_t start;   /**< Початок у байтах у вхідному UTF‑8 рядку. */
    size_t length;  /**< Довжина у байтах. */
    unsigned flags; /**< Стилі для діапазону (`TEXT_STYLE_*`). */
} text_span_t;

/** Додатковий стиль: закреслення. */
#define TEXT_STYLE_STRIKE 0x100u

/**
 * @brief Розміщує та рендерить текст у межах рамки.
 * @param text Вхідний текст (UTF‑8).
 * @param opts Опції верстки/рендерингу (обовʼязково `frame_width > 0`).
 * @param out [out] Контейнери контурів; ініціалізуються всередині.
 * @param lines_out [out] Якщо не `NULL` — масив метрик рядків (звільнити `text_layout_free_lines`).
 * @param lines_count [out] Кількість рядків у `lines_out`.
 * @param info [out] Загальні метрики рендерингу (може бути `NULL`).
 * @return 0 — успіх; -1 — помилка параметрів/виділення/шрифтів.
 */
int text_layout_render (
    const char *text,
    const text_layout_opts_t *opts,
    geom_paths_t *out,
    text_line_metrics_t **lines_out,
    size_t *lines_count,
    text_render_info_t *info);

/**
 * @brief Розміщує та рендерить текст із наборами спанів (діапазонних стилів).
 * @param text Вхідний текст (UTF‑8).
 * @param opts Опції верстки/рендерингу.
 * @param spans Масив діапазонів стилів; може бути `NULL`.
 * @param span_count Кількість елементів у `spans`.
 * @param out [out] Контейнери контурів; ініціалізуються всередині.
 * @param lines_out [out] Якщо не `NULL` — масив метрик рядків (звільняє викликач).
 * @param lines_count [out] Кількість рядків.
 * @param info [out] Метрики рендерингу.
 * @return 0 — успіх; -1 — помилка.
 */
int text_layout_render_spans (
    const char *text,
    const text_layout_opts_t *opts,
    const text_span_t *spans,
    size_t span_count,
    geom_paths_t *out,
    text_line_metrics_t **lines_out,
    size_t *lines_count,
    text_render_info_t *info);

/**
 * @brief Вивільняє масив метрик рядків, отриманий із `text_layout_render*`.
 */
void text_layout_free_lines (text_line_metrics_t *lines);

#endif
