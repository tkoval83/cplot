/**
 * @file planner.h
 * @brief Спрощений планувальник траєкторій для двовісного плотера.
 */
#ifndef CPLOT_PLANNER_H
#define CPLOT_PLANNER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Обмеження, що застосовуються до всієї побудови траєкторії.
 */
typedef struct {
    double max_speed_mm_s;        /**< Максимальна лінійна швидкість (мм/с). */
    double max_accel_mm_s2;       /**< Максимальне прискорення (мм/с²). */
    double cornering_distance_mm; /**< Допустиме відхилення у куті (еквівалент junction deviation). */
    double min_segment_mm;        /**< Мінімальна довжина сегмента, коротші зливаються. */
} planner_limits_t;

/**
 * @brief Вхідний сегмент для планувальника.
 */
typedef struct {
    double target_mm[2]; /**< Абсолютна кінцева точка сегмента у мм. */
    double feed_mm_s;    /**< Бажана швидкість руху для сегмента. */
    bool pen_down;       /**< true, якщо рух виконується з опущеним пером. */
} planner_segment_t;

/**
 * @brief Підготовлений блок траєкторії (після планування).
 */
typedef struct {
    double delta_mm[2];           /**< Зміщення сегмента у мм (x, y). */
    double length_mm;             /**< Довжина сегмента у мм. */
    double unit_vec[2];           /**< Нормований напрямок руху. */
    double start_speed_mm_s;      /**< Швидкість на початку сегмента. */
    double cruise_speed_mm_s;     /**< Максимальна досягнута швидкість у сегменті. */
    double end_speed_mm_s;        /**< Швидкість наприкінці сегмента. */
    double nominal_speed_mm_s;    /**< Номінальна (задана) швидкість сегмента. */
    double accel_mm_s2;           /**< Використане прискорення. */
    double accel_distance_mm;     /**< Ділянка розгону (мм). */
    double cruise_distance_mm;    /**< Ділянка руху з постійною швидкістю (мм). */
    double decel_distance_mm;     /**< Ділянка гальмування (мм). */
    bool pen_down;                /**< Чи має бути опущене перо на цьому відрізку. */
} plan_block_t;

/**
 * Ініціалізувати планувальник із заданими обмеженнями.
 */
bool planner_init (const planner_limits_t *limits);

/**
 * Скинути чергу планувальника (повністю очистити).
 */
void planner_reset (void);

/**
 * Оновити поточну позицію планувальника (наприклад, після home).
 */
void planner_sync_position (const double position_mm[2]);

/**
 * Додати сегмент у чергу планування.
 *
 * @return true, якщо сегмент успішно додано або злитий; false у разі переповнення.
 */
bool planner_enqueue (const planner_segment_t *segment);

/**
 * Перевірити, чи містить черга підготовлені блоки.
 */
bool planner_has_blocks (void);

/**
 * Взяти наступний готовий блок для подальшої дискретизації.
 *
 * @param out Вихідна структура (не NULL).
 * @return true, якщо блок видано; false якщо черга порожня.
 */
bool planner_pop (plan_block_t *out);

#ifdef __cplusplus
}
#endif

#endif /* CPLOT_PLANNER_H */
