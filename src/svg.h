/**
 * @file svg.h
 * @brief Серіалізація розкладки у формат SVG.
 */
#ifndef CPLOT_SVG_H
#define CPLOT_SVG_H

#include "drawing.h"

#ifdef __cplusplus
extern "C" {
#endif

int svg_render_layout (const drawing_layout_t *layout, bytes_t *out);

#ifdef __cplusplus
}
#endif

#endif /* CPLOT_SVG_H */
