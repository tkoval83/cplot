/**
 * @file png.h
 * @brief Заглушка генератора PNG-превʼю.
 */
#ifndef CPLOT_PNG_H
#define CPLOT_PNG_H

#include "drawing.h"

#ifdef __cplusplus
extern "C" {
#endif

int png_render_layout (const drawing_layout_t *layout, bytes_t *out);

#ifdef __cplusplus
}
#endif

#endif /* CPLOT_PNG_H */
