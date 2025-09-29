/**
 * @file plot.h
 * @brief Оркестрація побудови рухів: виконання плану через stepper/axidraw.
 */
#ifndef CPLOT_PLOT_H
#define CPLOT_PLOT_H

#include <stdbool.h>
#include <stddef.h>

#include "canvas.h"
#include "planner.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Виконує план руху на пристрої (або dry-run симуляцію).
 * @param blocks Масив блоків руху.
 * @param count Кількість блоків.
 * @param model Ідентифікатор моделі пристрою (NULL — типова).
 * @param dry_run true — без підключення до пристрою; тільки обчислення/журнали.
 * @param verbose true — докладні журнали.
 * @return 0 — успіх; 1 — помилка.
 */
int plot_execute_plan (
    const plan_block_t *blocks, size_t count, const char *model, bool dry_run, bool verbose);

/**
 * @brief Генерує план із розкладки та виконує його (або dry-run).
 * @param layout Розкладка, що містить фінальні шляхи полотна.
 * @param model Ідентифікатор моделі (NULL — типова).
 * @param dry_run true — без підключення; лише обчислення.
 * @param verbose true — докладні журнали.
 * @return 0 — успіх; 1 — помилка.
 */
int plot_canvas_execute (
    const canvas_layout_t *layout, const char *model, bool dry_run, bool verbose);

#ifdef __cplusplus
}
#endif

#endif
