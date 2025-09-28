/**
 * @file stepper.c
 * @brief Мінімальний перетворювач планувальника у команди AxiDraw.
 *
 * Примітка: Значення steps_per_mm тепер походить із профілю/налаштувань пристрою
 * (axidraw_settings_t) і не повинно підмінятися жорстко закодованою константою. Тут ми не
 * застосовуємо жорсткий fallback до історичного значення 80.0 — очікується, що вищий рівень
 * (контекст пристрою) передасть валідне значення через stepper_config_t або буде взято із
 * axidraw_settings_t динамічно.
 */

#include "stepper.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>

#include "log.h"

#define STEPPER_EPS_MM 1e-6
#define SPEED_EPS 1e-6
#define LM_INTERVAL_SEC 0.00004
#define LM_RATE_SCALE (2147483648.0 * LM_INTERVAL_SEC)

/**
 * @brief Опис однієї трапецієподібної фази для LM-команди та її відстеження.
 */
typedef struct {
    double distance_mm;
    double start_speed_mm_s;
    double end_speed_mm_s;
    int32_t steps_a;
    int32_t steps_b;
    double duration_s;
    unsigned long block_seq;
    size_t phase_index;
    size_t phase_count;
} stepper_phase_t;

/**
 * @brief Перетворити частоту кроків на значення rate для LM.
 *
 * @param steps_per_sec Частота кроків (кроки/с).
 * @return Значення rate, обмежене 31‑бітним діапазоном.
 */
static uint32_t rate_from_steps_per_sec (double steps_per_sec) {
    if (!(steps_per_sec > 0.0))
        return 0u;
    double rate = steps_per_sec * LM_RATE_SCALE;
    if (!isfinite (rate) || rate < 0.0)
        rate = 0.0;
    if (rate > 2147483647.0)
        rate = 2147483647.0;
    return (uint32_t)llround (rate);
}

/**
 * @brief Обмежити дійсне значення діапазоном signed 32‑bit.
 *
 * @param value Дійсне значення.
 * @return Округлене значення з урахуванням меж INT32.
 */
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
 * @brief Оцінити тривалість фази виходячи з початкової/кінцевої швидкостей.
 *
 * @param distance_mm Довжина фазової ділянки у мм.
 * @param start_speed Швидкість на вході у фазу (мм/с).
 * @param end_speed   Швидкість на виході з фази (мм/с).
 * @return Тривалість у секундах або 0.0, якщо розрахунок неможливий.
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
 * @brief Відправити підготовлену фазу руху у вигляді LM-команди.
 *
 * @param ctx           Контекст stepper-підсистеми.
 * @param phase         Фаза з попередньо розрахованими параметрами.
 * @param send_command  true → відправляти команду на пристрій; false → лише логувати.
 * @return true при успіху або коли рух не потребує кроків; false при помилці відправлення.
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

    uint32_t intervals = (uint32_t)llround (duration_s / LM_INTERVAL_SEC);
    if (intervals == 0)
        intervals = 1;

    uint32_t rate1 = 0u;
    uint32_t rate2 = 0u;
    int32_t accel1 = 0;
    int32_t accel2 = 0;

    if (phase->steps_a != 0) {
        double steps_per_mm = (double)phase->steps_a / phase->distance_mm;
        double start_rate = fabs (phase->start_speed_mm_s * steps_per_mm);
        double end_rate = fabs (phase->end_speed_mm_s * steps_per_mm);
        rate1 = rate_from_steps_per_sec (start_rate);
        uint32_t rate1_end = rate_from_steps_per_sec (end_rate);
        double accel = ((double)rate1_end - (double)rate1) / (double)intervals;
        accel1 = clamp_i32 (accel);
        if (accel1 == 0 && rate1_end != rate1)
            accel1 = (rate1_end > rate1) ? 1 : -1;
    }

    if (phase->steps_b != 0) {
        double steps_per_mm = (double)phase->steps_b / phase->distance_mm;
        double start_rate = fabs (phase->start_speed_mm_s * steps_per_mm);
        double end_rate = fabs (phase->end_speed_mm_s * steps_per_mm);
        rate2 = rate_from_steps_per_sec (start_rate);
        uint32_t rate2_end = rate_from_steps_per_sec (end_rate);
        double accel = ((double)rate2_end - (double)rate2) / (double)intervals;
        accel2 = clamp_i32 (accel);
        if (accel2 == 0 && rate2_end != rate2)
            accel2 = (rate2_end > rate2) ? 1 : -1;
    }

    const char *mode = (send_command && ctx->cfg.dev != NULL) ? "відправка" : "імітація";
    log_print (
        LOG_DEBUG,
        "крокувач.фаза: блок №%lu фаза %zu/%zu режим=%s відстань=%.4f початок=%.3f кінець=%.3f "
        "крокиA=%d крокиB=%d швидкістьA=%u прискоренняA=%d швидкістьB=%u прискоренняB=%d "
        "інтервалів=%u тривалість=%.4f",
        phase->block_seq, phase->phase_index + 1, phase->phase_count, mode, phase->distance_mm,
        phase->start_speed_mm_s, phase->end_speed_mm_s, phase->steps_a, phase->steps_b, rate1,
        accel1, rate2, accel2, intervals, duration_s);

    if (!send_command || ctx->cfg.dev == NULL)
        return true;

    int rc = axidraw_move_lowlevel (
        ctx->cfg.dev, rate1, phase->steps_a, accel1, rate2, phase->steps_b, accel2, 0);
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
 * @brief Ініціалізувати контекст stepper-підсистеми.
 *
 * @param ctx Контекст для заповнення.
 * @param cfg Початкові налаштування або NULL для типових.
 */
static double resolve_steps_per_mm (const stepper_config_t *cfg) {
    if (cfg && cfg->steps_per_mm > 0.0)
        return cfg->steps_per_mm;
    if (cfg && cfg->dev) {
        const axidraw_settings_t *s = axidraw_device_settings (cfg->dev);
        if (s && s->steps_per_mm > 0.0)
            return s->steps_per_mm;
    }
    /* Якщо коефіцієнт кроків на міліметр не задано у профілі, повертається 0.0. */
    LOGE ("крокувач: коефіцієнт кроків на міліметр не встановлено (потрібен профіль пристрою)");
    return 0.0;
}

void stepper_init (stepper_context_t *ctx, const stepper_config_t *cfg) {
    if (!ctx)
        return;
    if (cfg) {
        ctx->cfg = *cfg;
    } else {
        ctx->cfg.steps_per_mm = 0.0;
        ctx->cfg.dev = NULL;
    }
    /* Автоматично вирішуємо steps_per_mm, якщо ще не встановлено */
    if (!(ctx->cfg.steps_per_mm > 0.0))
        ctx->cfg.steps_per_mm = resolve_steps_per_mm (&ctx->cfg);
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
    const stepper_context_t *ctx,
    const plan_block_t *block,
    int32_t *steps_x_out,
    int32_t *steps_y_out) {
    double spmm = ctx->cfg.steps_per_mm;
    if (!(spmm > 0.0))
        spmm = resolve_steps_per_mm (&ctx->cfg);
    double steps_x = block->delta_mm[0] * spmm;
    double steps_y = block->delta_mm[1] * spmm;
    if (steps_x_out)
        *steps_x_out = (int32_t)llround (steps_x);
    if (steps_y_out)
        *steps_y_out = (int32_t)llround (steps_y);
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
    if (!ctx || !block)
        return true;
    if (block->length_mm < STEPPER_EPS_MM)
        return true;

    int32_t steps_x = 0;
    int32_t steps_y = 0;
    mm_to_steps (ctx, block, &steps_x, &steps_y);
    int32_t steps_a_total = steps_x + steps_y;
    int32_t steps_b_total = steps_x - steps_y;

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
    int64_t used_steps_a = 0;
    int64_t used_steps_b = 0;

    for (size_t i = 0; i < phase_count; ++i) {
        stepper_phase_t *phase = &phases[i];
        double fraction
            = (total_length > STEPPER_EPS_MM) ? (phase->distance_mm / total_length) : 0.0;
        if (i + 1 == phase_count) {
            phase->steps_a = steps_a_total - (int32_t)used_steps_a;
            phase->steps_b = steps_b_total - (int32_t)used_steps_b;
        } else {
            phase->steps_a = clamp_i32 ((double)steps_a_total * fraction);
            phase->steps_b = clamp_i32 ((double)steps_b_total * fraction);
        }
        used_steps_a += phase->steps_a;
        used_steps_b += phase->steps_b;

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
        "Крокувач: кроки X=%d Y=%d A=%d B=%d, тривалість≈%u мс", steps_x, steps_y, steps_a_total,
        steps_b_total, approx_duration_ms);
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
