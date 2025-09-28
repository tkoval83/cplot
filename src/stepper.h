/**
 * @file stepper.h
 * @brief Проста обгортка для відправлення сегментів на AxiDraw.
 */
#ifndef CPLOT_STEPPER_H
#define CPLOT_STEPPER_H

#include <stdbool.h>

#include "axidraw.h"
#include "planner.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Конфігурація перетворення мм → кроки для блока stepper.
 */
typedef struct {
    double steps_per_mm;   /**< Кількість кроків на один міліметр (для осей X/Y). */
    axidraw_device_t *dev; /**< Пристрій AxiDraw; може бути NULL для dry-run. */
} stepper_config_t;

/**
 * @brief Контекст виконання stepper.
 */
typedef struct {
    stepper_config_t cfg;
    unsigned long emitted_blocks; /**< Лічильник відправлених блоків. */
} stepper_context_t;

/**
 * Ініціалізувати контекст stepper.
 */
void stepper_init (stepper_context_t *ctx, const stepper_config_t *cfg);

/**
 * Відправити підготовлений блок руху на пристрій або в dry-run лог.
 *
 * @param ctx     Контекст stepper.
 * @param block   Сформований планувальником блок.
 * @param dry_run true, якщо слід лише залогувати команду без взаємодії з пристроєм.
 * @return true при успіху; false, якщо сталася помилка відправлення.
 */
bool stepper_submit_block (stepper_context_t *ctx, const plan_block_t *block, bool dry_run);

#ifdef __cplusplus
}
#endif

#endif /* CPLOT_STEPPER_H */
