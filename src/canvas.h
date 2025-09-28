/**
 * @file canvas.h
 * @brief Заготовка для майбутньої системи розкладки (canvas).
 */
#ifndef CPLOT_CANVAS_H
#define CPLOT_CANVAS_H

#include <stdbool.h>
#include <stddef.h>

#include "config.h"
#include "geom.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Параметри побудови розкладки.
 */
typedef struct {
    double paper_w_mm;         /**< Ширина паперу (мм). */
    double paper_h_mm;         /**< Висота паперу (мм). */
    double margin_top_mm;      /**< Верхнє поле (мм). */
    double margin_right_mm;    /**< Праве поле (мм). */
    double margin_bottom_mm;   /**< Нижнє поле (мм). */
    double margin_left_mm;     /**< Ліве поле (мм). */
    orientation_t orientation; /**< Орієнтація сторінки. */
    const char *font_family;   /**< Родина шрифтів (може бути NULL). */
} canvas_options_t;

/**
 * @brief Параметри, що описують розкладку сторінки (після трансформацій).
 */
typedef struct {
    orientation_t orientation; /**< Застосована орієнтація. */
    double paper_w_mm;         /**< Фізична ширина паперу (мм). */
    double paper_h_mm;         /**< Фізична висота паперу (мм). */
    double margin_top_mm;      /**< Верхнє поле (мм). */
    double margin_right_mm;    /**< Праве поле (мм). */
    double margin_bottom_mm;   /**< Нижнє поле (мм). */
    double margin_left_mm;     /**< Ліве поле (мм). */
    double frame_w_mm;         /**< Доступна ширина робочої області (мм). */
    double frame_h_mm;         /**< Доступна висота робочої області (мм). */
    double start_x_mm;         /**< Абсолютна координата старту (мм). */
    double start_y_mm;         /**< Абсолютна координата старту (мм). */
    geom_bbox_t bounds_mm;     /**< Межі трансформованих шляхів (мм). */
    geom_paths_t paths_mm;     /**< Трансформовані шляхи у координатах пристрою (мм). */
} canvas_layout_t;

/**
 * @brief Коди результату роботи модуля canvas.
 */
typedef enum {
    CANVAS_STATUS_OK = 0,             /**< Планування успішне. */
    CANVAS_STATUS_INVALID_INPUT = 1,  /**< Некоректні параметри (поля, розміри тощо). */
    CANVAS_STATUS_INTERNAL_ERROR = 2, /**< Внутрішня помилка (нестача пам'яті тощо). */
} canvas_status_t;

/**
 * @brief Побудувати набір шляхів для вхідного тексту.
 *
 * @param options        Параметри полотна (папір, поля, орієнтація).
 * @param source_paths   Вихідні шляхи (логічні координати, одиниці як у source_paths->units).
 * @param out_layout     Результат із трансформованими шляхами (у мм) та метаданими початку/меж.
 * @return Код результату з canvas_status_t.
 */
canvas_status_t canvas_layout_document (
    const canvas_options_t *options, const geom_paths_t *source_paths, canvas_layout_t *out_layout);

/**
 * @brief Звільнити ресурси, що належать плану.
 */
void canvas_layout_dispose (canvas_layout_t *layout);

#ifdef __cplusplus
}
#endif

#endif /* CPLOT_CANVAS_H */
