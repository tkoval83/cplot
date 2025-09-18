/**
 * @file canvas.h
 * @brief Заготовка для майбутньої системи розкладки (canvas).
 */
#ifndef CPLOT_CANVAS_H
#define CPLOT_CANVAS_H

#include <stddef.h>
#include <stdbool.h>

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Параметри побудови розкладки.
 */
typedef struct {
    double paper_w_mm;      /**< Ширина паперу (мм). */
    double paper_h_mm;      /**< Висота паперу (мм). */
    double margin_top_mm;   /**< Верхнє поле (мм). */
    double margin_right_mm; /**< Праве поле (мм). */
    double margin_bottom_mm;/**< Нижнє поле (мм). */
    double margin_left_mm;  /**< Ліве поле (мм). */
    orientation_t orientation; /**< Орієнтація сторінки. */
    const char *font_family;   /**< Родина шрифтів (може бути NULL). */
} canvas_options_t;

/**
 * @brief Опис одного відрізка траєкторії, який повинен породити canvas.
 */
typedef struct {
    double x_mm;      /**< Абсолютна координата X після трансформації (мм). */
    double y_mm;      /**< Абсолютна координата Y після трансформації (мм). */
    bool pen_down;    /**< Стан пера для цього вузла. */
    double feed_mm_s; /**< Бажана швидкість переходу. */
} canvas_vertex_t;

/**
 * @brief Шлях, що складається з послідовності вершин.
 */
typedef struct {
    canvas_vertex_t *vertices; /**< Масив вершин. */
    size_t vertex_count;       /**< Кількість вершин. */
} canvas_path_t;

/**
 * @brief Результат планування полотна.
 */
typedef struct {
    canvas_path_t *paths; /**< Масив шляхів. */
    size_t path_count;    /**< Кількість шляхів. */
} canvas_plan_t;

/**
 * @brief Побудувати набір шляхів для вхідного тексту.
 *
 * @param text      Вхідні байти тексту (UTF-8).
 * @param text_len  Довжина в байтах.
 * @param options   Опції полотна.
 * @param out_plan  Вихідна структура для заповнення.
 * @return true, якщо планування виконано; false, якщо функціонал ще не реалізовано.
 */
bool canvas_plan_document (
    const char *text,
    size_t text_len,
    const canvas_options_t *options,
    canvas_plan_t *out_plan);

/**
 * @brief Звільнити ресурси, що належать плану.
 */
void canvas_plan_dispose (canvas_plan_t *plan);

#ifdef __cplusplus
}
#endif

#endif /* CPLOT_CANVAS_H */
