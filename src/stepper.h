/**
 * @file stepper.h
 * @brief Перетворення плану руху у кроки двигунів.
 * @defgroup stepper Крокові
 * @details
 * Приймає блоки траєкторії з планувальника та перетворює їх у кроки/частоти
 * для низькорівневої команди AxiDraw (через `axidraw_move_lowlevel`). Підтримує
 * dry‑run режим для оцінки тривалості без відправлення на пристрій.
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
 * @brief Конфігурація крокувача.
 */
typedef struct {
    axidraw_device_t *dev; /**< Відкритий пристрій AxiDraw або `NULL` для dry‑run. */
} stepper_config_t;

/**
 * @brief Поточний стан крокувача.
 */
typedef struct {
    stepper_config_t cfg;         /**< Активна конфігурація. */
    unsigned long emitted_blocks; /**< Лічильник успішно оброблених блоків. */
} stepper_context_t;

/**
 * @brief Ініціалізує контекст крокувача.
 * @param ctx [out] Контекст для ініціалізації.
 * @param cfg Початкова конфігурація; може бути `NULL` (все за замовчуванням).
 */
void stepper_init (stepper_context_t *ctx, const stepper_config_t *cfg);

/**
 * @brief Перетворює один блок плану у послідовність фаз і, за потреби, надсилає до пристрою.
 * @param ctx Контекст.
 * @param block Блок плану від планувальника.
 * @param dry_run Якщо `true` — лише обчислення/журнали без відправлення на пристрій.
 * @return true — успіх; false — помилка відправлення або параметрів.
 */
bool stepper_submit_block (stepper_context_t *ctx, const plan_block_t *block, bool dry_run);

#ifdef __cplusplus
}
#endif

#endif
