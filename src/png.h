/**
 * @file png.h
 * @brief Генерація PNG із векторної розкладки.
 * @defgroup png PNG
 * @ingroup drawing
 * @details
 * Модуль формує монохромне PNG‑зображення (8‑біт сірий) з контурів у мм,
 * масштабованих згідно з фіксованою роздільністю. Призначено для швидкого
 * превʼю без зовнішніх залежностей (власна реалізація PNG+zlib‑стиснення).
 */
#ifndef CPLOT_PNG_H
#define CPLOT_PNG_H

#include "drawing.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Рендерить PNG‑превʼю у памʼять.
 * @param layout Вхідна розкладка сторінки та шляхів у мм.
 * @param out [out] Буфер байтів із PNG: `out->bytes` виділяється всередині через `malloc()`
 *                  і належить викликачеві; `out->len` — розмір у байтах.
 * @return 0 — успіх; 1 — помилка аргументів або виділення/формування.
 */
int png_render_layout (const drawing_layout_t *layout, bytes_t *out);

#ifdef __cplusplus
}
#endif

#endif
