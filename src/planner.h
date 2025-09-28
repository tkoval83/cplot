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
    double max_speed_mm_s;  /**< Максимальна лінійна швидкість (мм/с). */
    double max_accel_mm_s2; /**< Максимальне прискорення (мм/с²). */
    double
        cornering_distance_mm; /**< Допустиме відхилення у куті (еквівалент junction deviation). */
    double min_segment_mm;     /**< Мінімальна довжина сегмента, коротші зливаються. */
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
    unsigned long seq;         /**< Послідовний ідентифікатор блоку. */
    double delta_mm[2];        /**< Зміщення сегмента у мм (x, y). */
    double length_mm;          /**< Довжина сегмента у мм. */
    double unit_vec[2];        /**< Нормований напрямок руху. */
    double start_speed_mm_s;   /**< Швидкість на початку сегмента. */
    double cruise_speed_mm_s;  /**< Максимальна досягнута швидкість у сегменті. */
    double end_speed_mm_s;     /**< Швидкість наприкінці сегмента. */
    double nominal_speed_mm_s; /**< Номінальна (задана) швидкість сегмента. */
    double accel_mm_s2;        /**< Використане прискорення. */
    double accel_distance_mm;  /**< Ділянка розгону (мм). */
    double cruise_distance_mm; /**< Ділянка руху з постійною швидкістю (мм). */
    double decel_distance_mm;  /**< Ділянка гальмування (мм). */
    bool pen_down;             /**< Чи має бути опущене перо на цьому відрізку. */
} plan_block_t;

/**
 * Побудувати повний план руху для переданого шляху.
 *
 * Планувальник проходить масив сегментів, застосовуючи обмеження швидкості,
 * прискорення і злиття коротких відрізків, та повертає масив блоків, готових
 * до передачі у stepper.
 *
 * @param limits            Обмеження на рух (не NULL).
 * @param start_position_mm Початкова позиція X/Y у мм (NULL → {0,0}).
 * @param segments          Масив сегментів траєкторії (не NULL).
 * @param segment_count     Кількість сегментів у масиві.
 * @param out_blocks        Вихід: масив блоків (виділяється функцією, caller звільняє через
 * free()).
 * @param out_count         Вихід: кількість блоків у масиві out_blocks.
 *
 * @return true при успіху; false у разі некоректних параметрів або помилки пам’яті.
 */
bool planner_plan (
    const planner_limits_t *limits,
    const double start_position_mm[2],
    const planner_segment_t *segments,
    size_t segment_count,
    plan_block_t **out_blocks,
    size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* CPLOT_PLANNER_H */
