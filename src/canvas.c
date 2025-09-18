/**
 * @file canvas.c
 * @brief Заглушка генератора розкладки до появи повної реалізації.
 */

#include "canvas.h"

#include <stdlib.h>
#include <string.h>

#include "log.h"

bool canvas_plan_document (
    const char *text,
    size_t text_len,
    const canvas_options_t *options,
    canvas_plan_t *out_plan) {
    (void)text;
    (void)text_len;
    (void)options;
    if (!out_plan) {
        LOGE ("canvas: не передано структуру для результату");
        return false;
    }
    memset (out_plan, 0, sizeof (*out_plan));
    LOGW ("canvas: планування ще не реалізовано");
    return false;
}

void canvas_plan_dispose (canvas_plan_t *plan) {
    if (!plan)
        return;
    for (size_t i = 0; i < plan->path_count; ++i) {
        free (plan->paths[i].vertices);
    }
    free (plan->paths);
    memset (plan, 0, sizeof (*plan));
}

