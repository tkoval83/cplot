/**
 * @file stepper.c
 * @brief Реалізація перетворення блоків у кроки/таймінги.
 * @ingroup stepper
 * @details
 * Розбиває кожен блок руху на фази (розгін/круїз/гальмування), розподіляє кроки
 * між осями A/B та обчислює початкові швидкості/прискорення в одиницях EBB
 * (інтервали 40 мкс, фіксована‑кома частоти кроків). За `dry_run` команди на
 * пристрій не надсилаються, генеруються лише журнали.
 */

#include "stepper.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>

#include "log.h"

/** \brief Допуск для ігнорування дуже коротких відрізків, мм. */
#define STEPPER_EPS_MM 1e-6
/** \brief Допуск для перевірки нульових швидкостей. */
#define SPEED_EPS 1e-6

/**
 * @brief Одна фаза руху всередині блоку (розгін/круїз/гальмування).
 */
typedef struct {
    double distance_mm;        /**< Довжина цієї фази, мм. */
    double start_speed_mm_s;   /**< Початкова швидкість, мм/с. */
    double end_speed_mm_s;     /**< Кінцева швидкість, мм/с. */
    int32_t steps_a;           /**< Кроки на вісь A у фазі. */
    int32_t steps_b;           /**< Кроки на вісь B у фазі. */
    double duration_s;         /**< Тривалість фази, с. */
    unsigned long block_seq;   /**< Порядковий номер блоку. */
    size_t phase_index;        /**< Індекс фази в межах блоку. */
    size_t phase_count;        /**< Кількість фаз у блоці. */
} stepper_phase_t;

/**
 * @brief Перетворює кроки/с на EBB‑частоту у фіксованій комі.
 */
/* конверсія кроків/с у пристрій‑специфічні величини перенесена до axidraw */

/** \brief Безпечно зводить double до int32 з насиченням. */
static int32_t clamp_i32 (double value) {
    if (!isfinite (value))
        return 0;
    if (value > (double)INT32_MAX)
        return INT32_MAX;
    if (value < (double)INT32_MIN)
        return INT32_MIN;
    return (int32_t)llround (value);
}

/**
 * @brief Оцінює тривалість фази за математикою рівноприскореного руху.
 */
static double phase_duration_s (double distance_mm, double start_speed, double end_speed) {
    if (!(distance_mm > 0.0))
        return 0.0;
    double sum = start_speed + end_speed;
    if (sum > SPEED_EPS)
        return (2.0 * distance_mm) / sum;
    double fallback = fmax (start_speed, end_speed);
    if (fallback > SPEED_EPS)
        return distance_mm / fallback;
    return 0.0;
}

/**
 * @brief Готує та (за потреби) відправляє одну LowLevelMove фазу до пристрою.
 * @param ctx Контекст крокувача.
 * @param phase Опис фази.
 * @param send_command Якщо `true` і `ctx->cfg.dev` заданий — викликає `axidraw_move_lowlevel`.
 * @return true — успіх; false — помилка валідації або відправлення.
 */
static bool
stepper_emit_phase (stepper_context_t *ctx, const stepper_phase_t *phase, bool send_command) {
    if (!ctx || !phase)
        return false;
    if (phase->distance_mm <= STEPPER_EPS_MM || (phase->steps_a == 0 && phase->steps_b == 0))
        return true;

    double duration_s = phase->duration_s;
    if (!(duration_s > 0.0)) {
        LOGE ("Крокувач: некоректна тривалість фази руху");
        return false;
    }

    const char *mode = (send_command && ctx->cfg.dev != NULL) ? "відправка" : "імітація";
    log_print (
        LOG_DEBUG,
        "крокувач.фаза: блок №%lu фаза %zu/%zu режим=%s відстань=%.4f початок=%.3f кінець=%.3f "
        "крокиA=%d крокиB=%d тривалість=%.4f",
        phase->block_seq, phase->phase_index + 1, phase->phase_count, mode, phase->distance_mm,
        phase->start_speed_mm_s, phase->end_speed_mm_s, phase->steps_a, phase->steps_b,
        duration_s);

    if (!send_command || ctx->cfg.dev == NULL)
        return true;

    int rc = axidraw_move_lowlevel_phase_xy (
        ctx->cfg.dev, phase->distance_mm, phase->start_speed_mm_s, phase->end_speed_mm_s,
        phase->steps_a, phase->steps_b, duration_s);
    if (rc != 0) {
        LOGE ("Не вдалося відправити рух до пристрою (код %d)", rc);
#ifdef DEBUG
        log_print (LOG_ERROR, "крокувач: помилка відправлення фази (код %d)", rc);
#endif
        return false;
    }
    return true;
}

/**
 * @copydoc stepper_init
 */
void stepper_init (stepper_context_t *ctx, const stepper_config_t *cfg) {
    if (!ctx)
        return;
    if (cfg) {
        ctx->cfg = *cfg;
    } else {
        ctx->cfg.dev = NULL;
    }
    ctx->emitted_blocks = 0;
}

/** \brief Конвертує зміщення блоку у кроки по осях X/Y через AxiDraw. */
static void mm_to_steps (
    const stepper_context_t *ctx, const plan_block_t *block, int32_t *steps_x_out,
    int32_t *steps_y_out) {
    int32_t sx = 0, sy = 0;
    if (ctx && ctx->cfg.dev) {
        sx = axidraw_mm_to_steps (ctx->cfg.dev, block->delta_mm[0]);
        sy = axidraw_mm_to_steps (ctx->cfg.dev, block->delta_mm[1]);
    } else {
        LOGE (
            "крокувач: відсутній пристрій для конвертації мм→кроки (потрібен профіль пристрою)");
    }
    if (steps_x_out)
        *steps_x_out = sx;
    if (steps_y_out)
        *steps_y_out = sy;
}

/**
 * @copydoc stepper_submit_block
 */
bool stepper_submit_block (stepper_context_t *ctx, const plan_block_t *block, bool dry_run) {
    if (!ctx || !block)
        return true;
    if (block->length_mm < STEPPER_EPS_MM)
        return true;

    int32_t steps_x_total = 0;
    int32_t steps_y_total = 0;
    mm_to_steps (ctx, block, &steps_x_total, &steps_y_total);

    stepper_phase_t phases[3];
    size_t phase_count = 0;

    if (block->accel_distance_mm > STEPPER_EPS_MM) {
        size_t idx = phase_count++;
        phases[idx] = (stepper_phase_t){
            .distance_mm = block->accel_distance_mm,
            .start_speed_mm_s = block->start_speed_mm_s,
            .end_speed_mm_s = block->cruise_speed_mm_s,
            .block_seq = block->seq,
            .phase_index = idx,
        };
    }
    if (block->cruise_distance_mm > STEPPER_EPS_MM) {
        size_t idx = phase_count++;
        phases[idx] = (stepper_phase_t){
            .distance_mm = block->cruise_distance_mm,
            .start_speed_mm_s = block->cruise_speed_mm_s,
            .end_speed_mm_s = block->cruise_speed_mm_s,
            .block_seq = block->seq,
            .phase_index = idx,
        };
    }
    if (block->decel_distance_mm > STEPPER_EPS_MM) {
        size_t idx = phase_count++;
        phases[idx] = (stepper_phase_t){
            .distance_mm = block->decel_distance_mm,
            .start_speed_mm_s = block->cruise_speed_mm_s,
            .end_speed_mm_s = block->end_speed_mm_s,
            .block_seq = block->seq,
            .phase_index = idx,
        };
    }

    if (phase_count == 0) {
        phases[0] = (stepper_phase_t){
            .distance_mm = block->length_mm,
            .start_speed_mm_s = block->start_speed_mm_s,
            .end_speed_mm_s = block->end_speed_mm_s,
            .block_seq = block->seq,
            .phase_index = 0,
        };
        phase_count = 1;
    }

    for (size_t i = 0; i < phase_count; ++i)
        phases[i].phase_count = phase_count;

    double total_length = block->length_mm;
    double total_duration_s = 0.0;
    int64_t used_steps_x = 0;
    int64_t used_steps_y = 0;

    for (size_t i = 0; i < phase_count; ++i) {
        stepper_phase_t *phase = &phases[i];
        double fraction
            = (total_length > STEPPER_EPS_MM) ? (phase->distance_mm / total_length) : 0.0;
        if (i + 1 == phase_count) {
            phase->steps_a = steps_x_total - (int32_t)used_steps_x;
            phase->steps_b = steps_y_total - (int32_t)used_steps_y;
        } else {
            phase->steps_a = clamp_i32 ((double)steps_x_total * fraction);
            phase->steps_b = clamp_i32 ((double)steps_y_total * fraction);
        }
        used_steps_x += phase->steps_a;
        used_steps_y += phase->steps_b;

        double duration
            = phase_duration_s (phase->distance_mm, phase->start_speed_mm_s, phase->end_speed_mm_s);
        if (!(duration > 0.0)) {
            double fallback = fmax (phase->start_speed_mm_s, phase->end_speed_mm_s);
            if (!(fallback > SPEED_EPS))
                fallback = fmax (block->cruise_speed_mm_s, block->nominal_speed_mm_s);
            if (!(fallback > SPEED_EPS))
                fallback = 1.0;
            duration = phase->distance_mm / fallback;
        }
        phase->duration_s = duration;
        total_duration_s += duration;
    }

    uint32_t approx_duration_ms = (uint32_t)llround (total_duration_s * 1000.0);

    LOGD (
        "Крокувач: блок %lu зміщення=(%.3f,%.3f) довжина=%.3f перо=%s", block->seq,
        block->delta_mm[0], block->delta_mm[1], block->length_mm, block->pen_down ? "так" : "ні");
    LOGD (
        "Крокувач: кроки X=%d Y=%d, тривалість≈%u мс", steps_x_total, steps_y_total,
        approx_duration_ms);
    log_print (
        LOG_DEBUG, "крокувач: блок=%lu довжина=%.3f крейсер=%.3f кількість_фаз=%zu", block->seq,
        block->length_mm, block->cruise_speed_mm_s, phase_count);

    bool send_cmd = (!dry_run && ctx->cfg.dev != NULL);
    for (size_t i = 0; i < phase_count; ++i) {
        if (!stepper_emit_phase (ctx, &phases[i], send_cmd))
            return false;
    }

    ++ctx->emitted_blocks;
    return true;
}
