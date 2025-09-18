/**
 * @file planner.c
 * @brief Спрощений планувальник траєкторії з урахуванням кута повороту.
 */

#include "planner.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "log.h"
#include "trace.h"

#define PLANNER_QUEUE_SIZE 32
#define EPSILON_MM 1e-6

/** @brief Внутрішній вузол черги планувальника. */
typedef struct {
    double target[2];
    double delta[2];
    double unit_vec[2];
    double length_mm;
    double nominal_speed;
    double entry_speed;
    double exit_speed;
    bool pen_down;
} planner_node_t;

static planner_limits_t g_limits;
static planner_node_t g_nodes[PLANNER_QUEUE_SIZE];
static size_t g_head;
static size_t g_tail;
static size_t g_count;
static double g_last_position[2];

/**
 * @brief Повернути попередній індекс у кільцевій черзі.
 *
 * @param idx Поточний індекс у масиві вузлів.
 * @return Індекс попереднього елемента.
 */
static size_t planner_prev_index (size_t idx) {
    return (idx == 0) ? (PLANNER_QUEUE_SIZE - 1) : (idx - 1);
}

/**
 * @brief Повернути наступний індекс у кільцевій черзі.
 *
 * @param idx Поточний індекс у масиві вузлів.
 * @return Наступний індекс із урахуванням циклічності.
 */
static size_t planner_next_index (size_t idx) {
    return (idx + 1) % PLANNER_QUEUE_SIZE;
}

/** @brief Перевірити, чи заповнений буфер вузлів. */
static bool planner_is_full (void) {
    return g_count == PLANNER_QUEUE_SIZE;
}

/** @brief Отримати останній доданий вузол (тільки читання). */
static const planner_node_t *planner_last_node (void) {
    if (g_count == 0)
        return NULL;
    size_t idx = planner_prev_index (g_head);
    return &g_nodes[idx];
}

/** @brief Отримати останній вузол із можливістю модифікації. */
static planner_node_t *planner_last_node_mut (void) {
    if (g_count == 0)
        return NULL;
    size_t idx = planner_prev_index (g_head);
    return &g_nodes[idx];
}

/**
 * @brief Повернути додатне значення або запасний варіант.
 *
 * @param value Значення для перевірки.
 * @param fallback Значення, що повертається при некоректності.
 */
static double clamp_positive (double value, double fallback) {
    if (!(value > 0.0) || isinf (value) || isnan (value))
        return fallback;
    return value;
}

/**
 * @brief Обчислити максимально безпечну швидкість у точці стику сегментів.
 *
 * @param prev Попередній сегмент шляху.
 * @param curr Поточний сегмент шляху.
 * @return Відкоригована швидкість у мм/с.
 */
static double compute_junction_speed (const planner_node_t *prev, const planner_node_t *curr) {
    if (!prev || !curr)
        return 0.0;
    if (g_limits.cornering_distance_mm <= 0.0)
        return 0.0;
    double dot = prev->unit_vec[0] * curr->unit_vec[0] + prev->unit_vec[1] * curr->unit_vec[1];
    if (!isfinite (dot))
        return 0.0;
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
 * @brief Додати вузол до буфера та оновити індекс head.
 *
 * @param node Вузол для збереження.
 */
static void planner_store_node (const planner_node_t *node) {
    g_nodes[g_head] = *node;
    g_head = planner_next_index (g_head);
    ++g_count;
}

/**
 * @brief Зняти вузол з хвоста буфера.
 *
 * @param out Структура для вихідних даних.
 */
static void planner_pop_tail (planner_node_t *out) {
    if (g_count == 0)
        return;
    *out = g_nodes[g_tail];
    g_tail = planner_next_index (g_tail);
    --g_count;
}

/**
 * @brief Ініціалізувати планувальник і застосувати обмеження.
 *
 * @param limits Параметри обмежень (не NULL).
 * @return true, якщо ліміти валідні; false, якщо відсутні або некоректні.
 */
bool planner_init (const planner_limits_t *limits) {
    if (!limits) {
        LOGE ("planner: не передано обмеження");
        return false;
    }
    if (!(limits->max_speed_mm_s > 0.0) || !(limits->max_accel_mm_s2 > 0.0)) {
        LOGE ("planner: швидкість та прискорення повинні бути додатними");
        return false;
    }
    if (!(limits->cornering_distance_mm >= 0.0) || !(limits->min_segment_mm >= 0.0)) {
        LOGE ("planner: кути та мінімальна довжина не можуть бути від’ємними");
        return false;
    }

    g_limits = *limits;
    planner_reset ();
    return true;
}

/** @brief Очистити чергу планувальника та скинути позицію. */
void planner_reset (void) {
    g_head = 0;
    g_tail = 0;
    g_count = 0;
    g_last_position[0] = 0.0;
    g_last_position[1] = 0.0;
}

/**
 * @brief Синхронізувати внутрішню позицію з зовнішнім станом.
 *
 * @param position_mm Координати X/Y у мм.
 */
void planner_sync_position (const double position_mm[2]) {
    if (!position_mm)
        return;
    g_last_position[0] = position_mm[0];
    g_last_position[1] = position_mm[1];
    if (g_count == 0)
        return;
    planner_node_t *last = planner_last_node_mut ();
    if (last) {
        last->target[0] = position_mm[0];
        last->target[1] = position_mm[1];
    }
}

bool planner_enqueue (const planner_segment_t *segment) {
    if (!segment)
        return false;
    if (planner_is_full ()) {
        LOGW ("Черга планувальника переповнена");
        return false;
    }

    double start[2];
    if (g_count == 0) {
        start[0] = g_last_position[0];
        start[1] = g_last_position[1];
    } else {
        const planner_node_t *last = planner_last_node ();
        start[0] = last->target[0];
        start[1] = last->target[1];
    }

    double delta[2];
    delta[0] = segment->target_mm[0] - start[0];
    delta[1] = segment->target_mm[1] - start[1];
    double length_mm = hypot (delta[0], delta[1]);

    if (length_mm < g_limits.min_segment_mm) {
        planner_node_t *last_mut = planner_last_node_mut ();
        if (last_mut) {
            double start_x = last_mut->target[0] - last_mut->delta[0];
            double start_y = last_mut->target[1] - last_mut->delta[1];
            last_mut->target[0] = segment->target_mm[0];
            last_mut->target[1] = segment->target_mm[1];
            last_mut->delta[0] = last_mut->target[0] - start_x;
            last_mut->delta[1] = last_mut->target[1] - start_y;
            last_mut->length_mm = hypot (last_mut->delta[0], last_mut->delta[1]);
            if (last_mut->length_mm > EPSILON_MM) {
                double inv_len = 1.0 / last_mut->length_mm;
                last_mut->unit_vec[0] = last_mut->delta[0] * inv_len;
                last_mut->unit_vec[1] = last_mut->delta[1] * inv_len;
            } else {
                last_mut->unit_vec[0] = 0.0;
                last_mut->unit_vec[1] = 0.0;
            }
            double new_nominal = clamp_positive (segment->feed_mm_s, g_limits.max_speed_mm_s);
            if (new_nominal > g_limits.max_speed_mm_s)
                new_nominal = g_limits.max_speed_mm_s;
            if (last_mut->nominal_speed <= 0.0 || new_nominal < last_mut->nominal_speed)
                last_mut->nominal_speed = new_nominal;
            last_mut->pen_down = segment->pen_down;
            if (last_mut->entry_speed > last_mut->nominal_speed)
                last_mut->entry_speed = last_mut->nominal_speed;
            if (last_mut->exit_speed > last_mut->nominal_speed)
                last_mut->exit_speed = last_mut->nominal_speed;
        }
        g_last_position[0] = segment->target_mm[0];
        g_last_position[1] = segment->target_mm[1];
#ifdef DEBUG
        trace_write (
            LOG_DEBUG,
            "planner: злиття короткого сегмента (%.6f мм) → позиція (%.3f, %.3f)",
            length_mm,
            segment->target_mm[0],
            segment->target_mm[1]);
#endif
        return true;
    }

    planner_node_t node;
    memset (&node, 0, sizeof (node));
    node.target[0] = segment->target_mm[0];
    node.target[1] = segment->target_mm[1];
    node.delta[0] = delta[0];
    node.delta[1] = delta[1];
    node.length_mm = length_mm;
    node.pen_down = segment->pen_down;

    double inv_length = 1.0 / length_mm;
    node.unit_vec[0] = delta[0] * inv_length;
    node.unit_vec[1] = delta[1] * inv_length;

    double nominal = segment->feed_mm_s;
    nominal = clamp_positive (nominal, g_limits.max_speed_mm_s);
    if (nominal > g_limits.max_speed_mm_s)
        nominal = g_limits.max_speed_mm_s;
    node.nominal_speed = nominal;
    node.entry_speed = 0.0;
    node.exit_speed = 0.0;

    if (g_count == 0) {
        node.entry_speed = 0.0;
    } else {
        planner_node_t *prev = planner_last_node_mut ();
        double junction = compute_junction_speed (prev, &node);
        double clamped = fmin (junction, fmin (prev->nominal_speed, node.nominal_speed));
        if (clamped < 0.0)
            clamped = 0.0;
        prev->exit_speed = fmin (prev->nominal_speed, clamped);
        node.entry_speed = prev->exit_speed;
    }

    planner_store_node (&node);
    g_last_position[0] = node.target[0];
    g_last_position[1] = node.target[1];
#ifdef DEBUG
    trace_write (
        LOG_DEBUG,
        "planner: enqueue len=%.3f entry=%.3f nominal=%.3f pen=%d target=(%.3f,%.3f)",
        node.length_mm,
        node.entry_speed,
        node.nominal_speed,
        node.pen_down,
        node.target[0],
        node.target[1]);
#endif
    return true;
}

/**
 * @brief Перевірити, чи є готові до видачі блоки у черзі планувальника.
 *
 * @return true, якщо у буфері є хоча б один сегмент; інакше false.
 */
bool planner_has_blocks (void) {
    return g_count > 0;
}

/**
 * @brief Розкласти сегмент на трапецієподібний профіль розгону/гальмування.
 *
 * @param node Внутрішній вузол з параметрами сегмента.
 * @param out  Підготовлений блок, який доповнюється параметрами профілю.
 */
static void compute_trapezoid_profile (planner_node_t *node, plan_block_t *out) {
    const double length = node->length_mm;
    const double v0 = node->entry_speed;
    const double v1 = node->exit_speed;
    double vmax = node->nominal_speed;
    if (!(vmax > 0.0))
        vmax = g_limits.max_speed_mm_s;
    double accel = g_limits.max_accel_mm_s2;
    if (!(accel > 0.0))
        accel = 1000.0;

    double accel_dist = fmax (0.0, (vmax * vmax - v0 * v0) / (2.0 * accel));
    double decel_dist = fmax (0.0, (vmax * vmax - v1 * v1) / (2.0 * accel));
    double cruise_speed = vmax;

    if (accel_dist + decel_dist > length) {
        double numerator = 2.0 * accel * length + v0 * v0 + v1 * v1;
        double v_cap = sqrt (fmax (numerator / 2.0, 0.0));
        if (v_cap < fmax (v0, v1))
            v_cap = fmax (v0, v1);
        if (v_cap < 0.0)
            v_cap = 0.0;
        cruise_speed = fmin (v_cap, vmax);
        accel_dist = fmax (0.0, (cruise_speed * cruise_speed - v0 * v0) / (2.0 * accel));
        decel_dist = fmax (0.0, (cruise_speed * cruise_speed - v1 * v1) / (2.0 * accel));
    }

    double cruise_dist = length - accel_dist - decel_dist;
    if (cruise_dist < 0.0)
        cruise_dist = 0.0;

    out->accel_distance_mm = accel_dist;
    out->decel_distance_mm = decel_dist;
    out->cruise_distance_mm = cruise_dist;
    out->cruise_speed_mm_s = cruise_speed;
    out->accel_mm_s2 = accel;
}

/**
 * @brief Взяти наступний підготовлений блок із черги та згенерувати профіль руху.
 *
 * @param out Вихідна структура (не NULL), у яку буде записано результат.
 * @return true, якщо блок отримано; false, якщо черга порожня або out == NULL.
 */
bool planner_pop (plan_block_t *out) {
    if (!out || g_count == 0)
        return false;

    planner_node_t node;
    planner_pop_tail (&node);

    memset (out, 0, sizeof (*out));
    out->delta_mm[0] = node.delta[0];
    out->delta_mm[1] = node.delta[1];
    out->length_mm = node.length_mm;
    out->unit_vec[0] = node.unit_vec[0];
    out->unit_vec[1] = node.unit_vec[1];
    out->start_speed_mm_s = node.entry_speed;
    out->end_speed_mm_s = node.exit_speed;
    out->nominal_speed_mm_s = node.nominal_speed;
    out->pen_down = node.pen_down;

    compute_trapezoid_profile (&node, out);

    // Після вилучення потрібно оновити останню позицію, якщо черга спорожніла.
    if (g_count == 0) {
        g_last_position[0] = node.target[0];
        g_last_position[1] = node.target[1];
    }
#ifdef DEBUG
    trace_write (
        LOG_DEBUG,
        "planner: pop len=%.3f start=%.3f end=%.3f cruise=%.3f pen=%d",
        out->length_mm,
        out->start_speed_mm_s,
        out->end_speed_mm_s,
        out->cruise_speed_mm_s,
        out->pen_down);
#endif

    return true;
}
