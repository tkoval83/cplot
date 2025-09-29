/**
 * @file markdown.h
 * @brief Спрощений рендеринг Markdown у контури (заголовки/акценти).
 * @defgroup markdown Markdown
 * @ingroup text
 * @details
 * Мінімальний підмодуль для перетворення обмеженої підмножини Markdown
 * (заголовки, абзаци, списки, цитати, прості таблиці, акценти `*`/`_`) у
 * контурні шляхи Hershey. Розкладка виконується текстовим рушієм з урахуванням
 * ширини кадру та вирівнювання. Усі геометричні величини — в міліметрах.
 */
#ifndef CPLOT_MARKDOWN_H
#define CPLOT_MARKDOWN_H

#include "geom.h"
#include "text.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Опції рендерингу Markdown.
 */
typedef struct markdown_opts {
    const char *family;      /**< Родина шрифтів Hershey для тексту (може бути `NULL`). */
    double base_size_pt;     /**< Базовий кегль у пунктах; якщо <=0 — використовується 14 pt. */
    double frame_width_mm;   /**< Ширина кадру для переносу рядків (мм). */
} markdown_opts_t;

/**
 * @brief Перетворює Markdown‑текст у геометричні контури.
 * @param text Вхідний Markdown (UTF‑8).
 * @param opts Опції рендерингу; обовʼязкові поля: `frame_width_mm`; `family`/`base_size_pt` —
 *            необовʼязкові (мають розумні типові значення).
 * @param out [out] Контейнер шляхів у мм; ініціалізується всередині функції.
 * @param info [out] Якщо не `NULL` — метрики рендерингу (висота рядка тощо) для
 *             останнього блоку.
 * @return 0 — успіх; 1/2 — помилка розбору або виділення памʼяті.
 */
int markdown_render_paths (
    const char *text, const markdown_opts_t *opts, geom_paths_t *out, text_render_info_t *info);

#ifdef __cplusplus
}
#endif

#endif
