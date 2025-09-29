/**
 * @file svg.h
 * @brief Генерація SVG із геометричних контурів.
 * @defgroup svg SVG
 * @ingroup drawing
 * @details
 * Створює легкий SVG‑макет для попереднього перегляду: білий фон, рамка сторінки
 * та контури як `path` з лініями. Всі одиниці — міліметри, атрибути `width`/`height`
 * та `viewBox` виставляються відповідно до параметрів сторінки.
 */
#ifndef CPLOT_SVG_H
#define CPLOT_SVG_H

#include "drawing.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Генерує SVG‑макет у памʼять.
 * @param layout Вхідна розкладка сторінки і шляхів у мм.
 * @param out [out] Буфер байтів із SVG: `out->bytes` виділяється `malloc()` і належить викликачеві;
 *                  `out->len` — довжина у байтах.
 * @return 0 — успіх; 1 — помилка аргументів або виділення/форматування.
 */
int svg_render_layout (const drawing_layout_t *layout, bytes_t *out);

#ifdef __cplusplus
}
#endif

#endif
