/**
 * @file planner.c
 * @brief Реалізація планувальника траєкторій і профілів швидкості.
 * @ingroup planner
 * @details
 * Формує безпечні швидкісні профілі (трапецієвидні/трикутні) для послідовності
 * векторних сегментів з урахуванням обмежень швидкості, прискорення і заокруглень
 * на стиках (cornering). Виконує обʼєднання дуже коротких колінеарних сегментів.
 */

#include "planner.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

/** \brief Допуск довжини для нульового сегмента, у мм. */
#define EPSILON_MM 1e-6

/**
 * @brief Внутрішній вузол планування (один сегмент із розрахованими межами швидкостей).
 */
typedef struct {
    double target[2];       /**< Кінцева точка сегмента (мм). */
    double delta[2];        /**< Вектор зміщення (мм). */
    double unit_vec[2];     /**< Одиничний напрямний вектор. */
    double length_mm;       /**< Довжина сегмента, мм. */
    double nominal_speed;   /**< Номінальна швидкість (обмежена feed/лімітами), мм/с. */
    double max_entry_speed; /**< Максимальна дозволена швидкість входу, мм/с. */
    double entry_speed;     /**< Обрана швидкість входу, мм/с. */
    double exit_speed;      /**< Обрана швидкість виходу, мм/с. */
    bool pen_down;          /**< Стан пера. */
    unsigned long seq;      /**< Порядковий номер. */
} planner_node_t;

/** \brief Поточні глобальні ліміти, що застосовуються при розрахунках. */
static planner_limits_t g_limits;

/**
 * @brief Повертає додатне скінченне значення або запасне.
 * @param value Вхідне значення.
 * @param fallback Запасне значення, якщо `value` не придатне.
 */
static double clamp_positive (double value, double fallback) {
    if (!(value > 0.0) || isinf (value) || isnan (value))
        return fallback;
    return value;
}

/**
 * @brief Обчислює ліміт швидкості на стику двох сегментів.
 * @param prev Попередній вузол.
 * @param curr Поточний вузол.
 * @return Максимальна швидкість входу згідно з `cornering_distance_mm` і кутом між векторами.
 */
static double compute_junction_speed (const planner_node_t *prev, const planner_node_t *curr) {
    if (!prev || !curr)
        return 0.0;
    double dot = prev->unit_vec[0] * curr->unit_vec[0] + prev->unit_vec[1] * curr->unit_vec[1];
    if (!isfinite (dot))
        return 0.0;
    if (g_limits.cornering_distance_mm <= 0.0)
        return (dot > 0.999999) ? clamp_positive (g_limits.max_speed_mm_s, 0.0) : 0.0;
    if (dot > 0.999999)
        return clamp_positive (g_limits.max_speed_mm_s, 0.0);
    if (dot < -0.999999)
        dot = -0.999999;
    double sin_theta_half = sqrt (0.5 * (1.0 - dot));
    if (sin_theta_half <= 1e-9)
        return clamp_positive (g_limits.max_speed_mm_s, 0.0);
    double numerator = g_limits.max_accel_mm_s2 * g_limits.cornering_distance_mm * sin_theta_half;
    double denom = 1.0 - sin_theta_half;
    if (denom <= 0.0)
        return 0.0;
    double limit = sqrt (numerator / denom);
    if (!isfinite (limit) || limit <= 0.0)
        return 0.0;
    if (limit > g_limits.max_speed_mm_s)
        limit = g_limits.max_speed_mm_s;
    return limit;
}

/**
 * @brief Обчислює параметри трапецієвидного профілю для сегмента.
 * @param node Джерельний вузол з довжиною та граничними швидкостями.
 * @param out [out] Блок для заповнення відстаней/швидкостей/прискорення.
 */
static void compute_trapezoid_profile (const planner_node_t *node, plan_block_t *out) {
    const double length = node->length_mm;
    double v0 = node->entry_speed;
    double v1 = node->exit_speed;
    if (!(v0 > 0.0))
        v0 = 0.0;
    if (!(v1 > 0.0))
        v1 = 0.0;
    if (v0 > g_limits.max_speed_mm_s)
        v0 = g_limits.max_speed_mm_s;
    if (v1 > g_limits.max_speed_mm_s)
        v1 = g_limits.max_speed_mm_s;
    if (!(length > 0.0)) {
        out->accel_distance_mm = 0.0;
        out->decel_distance_mm = 0.0;
        out->cruise_distance_mm = 0.0;
        out->cruise_speed_mm_s = fmax (fmax (v0, v1), 0.0);
        double accel_default = g_limits.max_accel_mm_s2;
        if (!(accel_default > 0.0))
            accel_default = 1000.0;
        out->accel_mm_s2 = accel_default;
        if (out->start_speed_mm_s > out->cruise_speed_mm_s)
            out->start_speed_mm_s = out->cruise_speed_mm_s;
        if (out->end_speed_mm_s > out->cruise_speed_mm_s)
            out->end_speed_mm_s = out->cruise_speed_mm_s;
        return;
    }

    double vmax = node->nominal_speed;
    if (!(vmax > 0.0) || vmax > g_limits.max_speed_mm_s)
        vmax = g_limits.max_speed_mm_s;
    double accel = g_limits.max_accel_mm_s2;
    if (!(accel > 0.0))
        accel = 1000.0;

    double accel_dist = fmax (0.0, (vmax * vmax - v0 * v0) / (2.0 * accel));
    double decel_dist = fmax (0.0, (vmax * vmax - v1 * v1) / (2.0 * accel));
    double cruise_speed = vmax;

    double sum_dist = accel_dist + decel_dist;
    if (sum_dist > length) {
        double numerator = fmax (0.0, 2.0 * accel * length + v0 * v0 + v1 * v1);
        double v_peak = sqrt (numerator / 2.0);
        if (v_peak < fmax (v0, v1))
            v_peak = fmax (v0, v1);
        if (v_peak > vmax)
            v_peak = vmax;
        cruise_speed = v_peak;
        accel_dist = fmax (0.0, (v_peak * v_peak - v0 * v0) / (2.0 * accel));
        decel_dist = fmax (0.0, (v_peak * v_peak - v1 * v1) / (2.0 * accel));
        sum_dist = accel_dist + decel_dist;
        if (sum_dist > length && sum_dist > 0.0) {
            double scale = length / sum_dist;
            accel_dist *= scale;
            decel_dist *= scale;
            sum_dist = accel_dist + decel_dist;
        }
    }

    double cruise_dist = length - sum_dist;
    if (cruise_dist < 0.0)
        cruise_dist = 0.0;

    out->accel_distance_mm = accel_dist;
    out->decel_distance_mm = decel_dist;
    out->cruise_distance_mm = cruise_dist;
    out->cruise_speed_mm_s = cruise_speed;
    out->accel_mm_s2 = accel;

    if (out->start_speed_mm_s > cruise_speed)
        out->start_speed_mm_s = cruise_speed;
    if (out->end_speed_mm_s > cruise_speed)
        out->end_speed_mm_s = cruise_speed;
}

/**
 * @brief Обчислює межі швидкості входу для кожного стику.
 */
static void compute_all_junction_limits (planner_node_t *nodes, size_t count) {
    if (!nodes || count == 0)
        return;
    nodes[0].max_entry_speed = fmax (0.0, fmin (nodes[0].nominal_speed, g_limits.max_speed_mm_s));
    for (size_t i = 1; i < count; ++i) {
        double junction = compute_junction_speed (&nodes[i - 1], &nodes[i]);
        double lim = junction;
        if (!(lim > 0.0))
            lim = 0.0;

        lim = fmin (lim, nodes[i - 1].nominal_speed);
        lim = fmin (lim, nodes[i].nominal_speed);
        lim = fmin (lim, g_limits.max_speed_mm_s);
        if (lim < 0.0)
            lim = 0.0;
        nodes[i].max_entry_speed = lim;
    }
}

/**
 * @brief Виконує двонапрямну корекцію швидкостей входу/виходу згідно з прискоренням.
 */
static void recompute_entry_exit_speeds (planner_node_t *nodes, size_t count) {
    if (!nodes || count == 0)
        return;

    const double accel = (g_limits.max_accel_mm_s2 > 0.0) ? g_limits.max_accel_mm_s2 : 1000.0;

    nodes[count - 1].exit_speed = 0.0;

    double v_allow = sqrt (fmax (0.0, 2.0 * accel * nodes[count - 1].length_mm));
    double v_entry = fmin (nodes[count - 1].max_entry_speed, v_allow);
    if (v_entry < 0.0)
        v_entry = 0.0;
    nodes[count - 1].entry_speed = v_entry;

    for (size_t idx = count - 1; idx-- > 0;) {

        nodes[idx].exit_speed = nodes[idx + 1].entry_speed;

        double v_next = nodes[idx].exit_speed;
        double v_max_by_decel
            = sqrt (fmax (0.0, v_next * v_next + 2.0 * accel * nodes[idx].length_mm));
        double v_cap = fmin (nodes[idx].max_entry_speed, v_max_by_decel);
        if (v_cap < 0.0)
            v_cap = 0.0;
        nodes[idx].entry_speed = v_cap;
    }

    for (size_t i = 0; i + 1 < count; ++i) {
        double v_curr = nodes[i].entry_speed;

        double v_allow_fwd = sqrt (fmax (0.0, v_curr * v_curr + 2.0 * accel * nodes[i].length_mm));
        if (nodes[i + 1].entry_speed > v_allow_fwd) {
            nodes[i + 1].entry_speed = v_allow_fwd;
        }

        nodes[i].exit_speed = nodes[i + 1].entry_speed;

        if (nodes[i].entry_speed > nodes[i].nominal_speed)
            nodes[i].entry_speed = nodes[i].nominal_speed;
        if (nodes[i].exit_speed > nodes[i].nominal_speed)
            nodes[i].exit_speed = nodes[i].nominal_speed;
    }

    if (nodes[count - 1].entry_speed > nodes[count - 1].nominal_speed)
        nodes[count - 1].entry_speed = nodes[count - 1].nominal_speed;
}

/** \brief Додає вузол до масиву та збільшує лічильник. */
static void store_node (planner_node_t *nodes, size_t *node_count, planner_node_t node) {
    nodes[*node_count] = node;
    ++(*node_count);
}

/**
 * @copydoc planner_plan
 */
bool planner_plan (
    const planner_limits_t *limits,
    const double start_position_mm[2],
    const planner_segment_t *segments,
    size_t segment_count,
    plan_block_t **out_blocks,
    size_t *out_count) {
    if (out_blocks)
        *out_blocks = NULL;
    if (out_count)
        *out_count = 0;

    if (!limits || !segments || !out_blocks || !out_count) {
        LOGE ("планувальник: некоректні параметри виклику");
        return false;
    }
    if (!(limits->max_speed_mm_s > 0.0) || !(limits->max_accel_mm_s2 > 0.0)) {
        LOGE ("планувальник: швидкість та прискорення повинні бути додатними");
        return false;
    }
    if (!(limits->cornering_distance_mm >= 0.0) || !(limits->min_segment_mm >= 0.0)) {
        LOGE ("планувальник: кути та мінімальна довжина не можуть бути від’ємними");
        return false;
    }

    if (segment_count == 0) {
        return true;
    }

    planner_node_t *nodes = calloc (segment_count, sizeof (*nodes));
    if (!nodes) {
        LOGE ("планувальник: неможливо виділити пам’ять під вузли");
        return false;
    }

    g_limits = *limits;

    double current_pos[2] = { 0.0, 0.0 };
    if (start_position_mm) {
        current_pos[0] = start_position_mm[0];
        current_pos[1] = start_position_mm[1];
    }

    bool pending_short = false;
    planner_segment_t pending_segment;
    double pending_start[2] = { 0.0, 0.0 };

    size_t node_count = 0;
    unsigned long next_seq = 0;

    size_t seg_index = 0;
    while (seg_index < segment_count || pending_short) {
        planner_segment_t segment;
        double start_point[2];
        bool was_pending = false;

        if (pending_short) {
            segment = pending_segment;
            start_point[0] = pending_start[0];
            start_point[1] = pending_start[1];
            pending_short = false;
            was_pending = true;
        } else {
            segment = segments[seg_index++];
            start_point[0] = current_pos[0];
            start_point[1] = current_pos[1];
        }

        double delta[2];
        delta[0] = segment.target_mm[0] - start_point[0];
        delta[1] = segment.target_mm[1] - start_point[1];
        double length_mm = hypot (delta[0], delta[1]);

        if (length_mm <= EPSILON_MM) {
            current_pos[0] = segment.target_mm[0];
            current_pos[1] = segment.target_mm[1];
            continue;
        }

        if (!was_pending && node_count == 0 && g_limits.min_segment_mm > 0.0
            && length_mm < g_limits.min_segment_mm) {
            pending_short = true;
            pending_segment = segment;
            pending_start[0] = start_point[0];
            pending_start[1] = start_point[1];
            current_pos[0] = segment.target_mm[0];
            current_pos[1] = segment.target_mm[1];
            continue;
        }

        bool merged = false;
        if (length_mm < g_limits.min_segment_mm && node_count > 0) {
            planner_node_t *last_node = &nodes[node_count - 1];
            double start_x = last_node->target[0] - last_node->delta[0];
            double start_y = last_node->target[1] - last_node->delta[1];
            double new_delta_x = segment.target_mm[0] - start_x;
            double new_delta_y = segment.target_mm[1] - start_y;
            double new_length = hypot (new_delta_x, new_delta_y);
            if (new_length > EPSILON_MM && last_node->pen_down == segment.pen_down) {
                double inv_new_len = 1.0 / new_length;
                double new_unit_x = new_delta_x * inv_new_len;
                double new_unit_y = new_delta_y * inv_new_len;
                double dot
                    = last_node->unit_vec[0] * new_unit_x + last_node->unit_vec[1] * new_unit_y;
                if (dot > 1.0)
                    dot = 1.0;
                if (dot >= 0.999) {
                    last_node->target[0] = segment.target_mm[0];
                    last_node->target[1] = segment.target_mm[1];
                    last_node->delta[0] = new_delta_x;
                    last_node->delta[1] = new_delta_y;
                    last_node->length_mm = new_length;
                    last_node->unit_vec[0] = new_unit_x;
                    last_node->unit_vec[1] = new_unit_y;
                    double new_nominal
                        = clamp_positive (segment.feed_mm_s, g_limits.max_speed_mm_s);
                    if (new_nominal > g_limits.max_speed_mm_s)
                        new_nominal = g_limits.max_speed_mm_s;
                    if (last_node->nominal_speed <= 0.0 || new_nominal < last_node->nominal_speed)
                        last_node->nominal_speed = new_nominal;

                    merged = true;
                }
            }
        }

        if (merged) {
            current_pos[0] = segment.target_mm[0];
            current_pos[1] = segment.target_mm[1];
            continue;
        }

        planner_node_t node;
        memset (&node, 0, sizeof (node));
        node.target[0] = segment.target_mm[0];
        node.target[1] = segment.target_mm[1];
        node.delta[0] = delta[0];
        node.delta[1] = delta[1];
        node.length_mm = length_mm;
        node.pen_down = segment.pen_down;

        double inv_length = 1.0 / length_mm;
        node.unit_vec[0] = delta[0] * inv_length;
        node.unit_vec[1] = delta[1] * inv_length;

        double nominal = clamp_positive (segment.feed_mm_s, g_limits.max_speed_mm_s);
        if (nominal > g_limits.max_speed_mm_s)
            nominal = g_limits.max_speed_mm_s;
        node.nominal_speed = nominal;
        node.seq = ++next_seq;
        node.max_entry_speed = 0.0;
        node.entry_speed = 0.0;
        node.exit_speed = 0.0;

        store_node (nodes, &node_count, node);

        current_pos[0] = segment.target_mm[0];
        current_pos[1] = segment.target_mm[1];
    }

    plan_block_t *blocks = calloc (node_count, sizeof (*blocks));
    if (!blocks) {
        free (nodes);
        LOGE ("планувальник: неможливо виділити пам’ять під блоки");
        return false;
    }

    compute_all_junction_limits (nodes, node_count);
    recompute_entry_exit_speeds (nodes, node_count);

    for (size_t i = 0; i < node_count; ++i) {
        const planner_node_t *node = &nodes[i];
        plan_block_t *block = &blocks[i];
        block->seq = node->seq;
        block->delta_mm[0] = node->delta[0];
        block->delta_mm[1] = node->delta[1];
        block->length_mm = node->length_mm;
        block->unit_vec[0] = node->unit_vec[0];
        block->unit_vec[1] = node->unit_vec[1];
        block->start_speed_mm_s = node->entry_speed;
        block->end_speed_mm_s = node->exit_speed;
        block->nominal_speed_mm_s = node->nominal_speed;
        block->pen_down = node->pen_down;

        compute_trapezoid_profile (node, block);

#ifdef DEBUG
        log_print (
            LOG_DEBUG,
            "планувальник: блок №%lu довжина=%.3f старт=%.3f кінець=%.3f крейсер=%.3f "
            "a/c/d=%.3f/%.3f/%.3f перо=%d",
            block->seq, block->length_mm, block->start_speed_mm_s, block->end_speed_mm_s,
            block->cruise_speed_mm_s, block->accel_distance_mm, block->cruise_distance_mm,
            block->decel_distance_mm, block->pen_down);
#endif
    }

    free (nodes);

    *out_blocks = blocks;
    *out_count = node_count;
    return true;
}
