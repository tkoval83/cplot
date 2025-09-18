/**
 * @file stepper.c
 * @brief Мінімальний перетворювач планувальника у команди AxiDraw.
 */

#include "stepper.h"

#include <math.h>
#include <stdio.h>

#include "log.h"

#define MIN_DURATION_MS 1u
#define MAX_DURATION_MS 16777215u
#define STEPPER_EPS_MM 1e-6

/**
 * @brief Ініціалізувати контекст stepper-підсистеми.
 *
 * @param ctx Контекст для заповнення.
 * @param cfg Початкові налаштування або NULL для типових.
 */
void stepper_init (stepper_context_t *ctx, const stepper_config_t *cfg) {
    if (!ctx)
        return;
    if (cfg)
        ctx->cfg = *cfg;
    else {
        ctx->cfg.steps_per_mm = AXIDRAW_STEPS_PER_MM;
        ctx->cfg.dev = NULL;
    }
    if (!(ctx->cfg.steps_per_mm > 0.0))
        ctx->cfg.steps_per_mm = AXIDRAW_STEPS_PER_MM;
    ctx->emitted_blocks = 0;
}

/**
 * @brief Перетворити зміщення у мм в кроки для осей X/Y.
 *
 * @param ctx         Контекст налаштувань stepper.
 * @param block       Підготовлений блок планувальника.
 * @param steps_x_out Вихідні кроки по осі X.
 * @param steps_y_out Вихідні кроки по осі Y.
 */
static void mm_to_steps (
    const stepper_context_t *ctx, const plan_block_t *block, int32_t *steps_x_out, int32_t *steps_y_out) {
    double steps_x = block->delta_mm[0] * ctx->cfg.steps_per_mm;
    double steps_y = block->delta_mm[1] * ctx->cfg.steps_per_mm;
    if (steps_x_out)
        *steps_x_out = (int32_t)llround (steps_x);
    if (steps_y_out)
        *steps_y_out = (int32_t)llround (steps_y);
}

/**
 * @brief Оцінити тривалість виконання блоку в мілісекундах.
 *
 * @param block Підготовлений блок.
 * @return Тривалість у мс з обрізанням до допустимого діапазону.
 */
static uint32_t estimate_duration_ms (const plan_block_t *block) {
    double speed = block->cruise_speed_mm_s;
    if (!(speed > 0.0))
        speed = block->nominal_speed_mm_s;
    if (!(speed > 0.0))
        speed = 10.0; /* запасне значення */
    double time_s = block->length_mm / speed;
    if (!(time_s > 0.0))
        time_s = MIN_DURATION_MS / 1000.0;
    uint32_t duration = (uint32_t)llround (time_s * 1000.0);
    if (duration < MIN_DURATION_MS)
        duration = MIN_DURATION_MS;
    if (duration > MAX_DURATION_MS)
        duration = MAX_DURATION_MS;
    return duration;
}

/**
 * @brief Передати блок траєкторії у виконавчий пристрій або у лог dry-run.
 *
 * @param ctx     Контекст stepper.
 * @param block   Блок, що містить відрізок руху.
 * @param dry_run true, якщо слід лише залогувати команду.
 * @return true при успіху; false, якщо відправлення на пристрій завершилося помилкою.
 */
bool stepper_submit_block (stepper_context_t *ctx, const plan_block_t *block, bool dry_run) {
    if (!ctx || !block || block->length_mm < STEPPER_EPS_MM)
        return true;

    int32_t steps_x = 0;
    int32_t steps_y = 0;
    mm_to_steps (ctx, block, &steps_x, &steps_y);
    int32_t steps_a = steps_x + steps_y;
    int32_t steps_b = steps_y - steps_x;
    uint32_t duration_ms = estimate_duration_ms (block);

    LOGD (
        "Stepper: блок %lu delta=(%.3f,%.3f) len=%.3f pen=%s", ctx->emitted_blocks + 1,
        block->delta_mm[0], block->delta_mm[1], block->length_mm, block->pen_down ? "так" : "ні");
    LOGD (
        "Stepper: steps X=%d Y=%d A=%d B=%d, duration=%u ms", steps_x, steps_y, steps_a, steps_b,
        duration_ms);

    if (dry_run || ctx->cfg.dev == NULL) {
        ++ctx->emitted_blocks;
        return true;
    }

    int rc = axidraw_move_corexy (ctx->cfg.dev, duration_ms, steps_a, steps_b);
    if (rc != 0) {
        LOGE ("Не вдалося відправити рух до AxiDraw (код %d)", rc);
        return false;
    }

    ++ctx->emitted_blocks;
    return true;
}
