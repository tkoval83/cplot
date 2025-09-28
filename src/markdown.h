/**
 * @file markdown.h
 * @brief Примітивний рендерер Markdown → шляхи (параграфи + заголовки).
 */
#ifndef CPLOT_MARKDOWN_H
#define CPLOT_MARKDOWN_H

#include "geom.h"
#include "text.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct markdown_opts
 * @brief Параметри рендерингу Markdown-документу.
 *
 * Структура задає базові налаштування для текстового оформлення та
 * компонування результату безпосередньо перед передачею на полотно.
 */
typedef struct markdown_opts {
    const char *family;    /**< Родина шрифтів або NULL → типова. */
    double base_size_pt;   /**< Базовий кегль для звичайного тексту. */
    double frame_width_mm; /**< Доступна ширина рядка (мм). */
} markdown_opts_t;

/**
 * @brief Розібрати спрощений Markdown і згенерувати контурні шляхи.
 *
 * Підтримувані блоки: заголовки #/##/###, параграфи, блок‑цитати (>),
 * невпорядковані списки (-, *, +) та впорядковані списки (1.) з вкладеністю.
 * Таблиці й кодові блоки поки ігноруються; підтримуються базові інлайнові стилі
 * (курсив, жирний, закреслення, підкреслення).
 *
 * @param text  Документ Markdown у UTF-8.
 * @param opts  Опції рендерингу (не NULL).
 * @param out   Вихідні шляхи (у мм); викликач звільняє через geom_paths_free().
 * @param info  Необов'язково: статистика рендеру (може бути NULL).
 * @return 0 успіх; 1 — помилка памʼяті або параметрів.
 */
int markdown_render_paths (
    const char *text, const markdown_opts_t *opts, geom_paths_t *out, text_render_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* CPLOT_MARKDOWN_H */
