/**
 * @file planner.h
 * @brief Планування траєкторії руху з обмеженнями швидкості/прискорення.
 * @defgroup planner Планувальник
 * @details
 * Планувальник будує послідовність відрізків руху з урахуванням глобальних
 * лімітів (максимальні швидкість/прискорення, радіус заокруглення на стиках)
 * і властивостей вхідних сегментів. На виході формується масив блоків із
 * профілями швидкості (трапеція/трикутник) для кожного сегмента.
 */
#ifndef CPLOT_PLANNER_H
#define CPLOT_PLANNER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Глобальні обмеження для планування рухів.
 */
typedef struct {
    double max_speed_mm_s;        /**< Максимальна лінійна швидкість, мм/с. */
    double max_accel_mm_s2;       /**< Максимальне прискорення, мм/с². */
    double cornering_distance_mm; /**< Ефективна довжина заокруглення на стиках, мм. 0 — жорстка зупинка. */
    double min_segment_mm;        /**< Мінімальна довжина сегмента; коротші можуть зливатися, мм. */
} planner_limits_t;

/**
 * @brief Вхідний сегмент руху у міліметрах.
 */
typedef struct {
    double target_mm[2]; /**< Цільова позиція (X,Y) у мм у світовій системі. */
    double feed_mm_s;    /**< Бажана швидкість подачі для сегмента, мм/с. */
    bool pen_down;       /**< Прапорець стану пера (1 — опущене, 0 — підняте). */
} planner_segment_t;

/**
 * @brief Розкладений блок плану з профілями швидкості.
 */
typedef struct {
    unsigned long seq;       /**< Порядковий номер блоку (зростаючий). */
    double delta_mm[2];      /**< Вектор зміщення (X,Y) у мм. */
    double length_mm;        /**< Довжина сегмента у мм. */
    double unit_vec[2];      /**< Одиничний напрямний вектор (X,Y). */
    double start_speed_mm_s; /**< Швидкість на початку сегмента, мм/с. */
    double cruise_speed_mm_s;/**< Крейсерська швидкість (плато), мм/с. */
    double end_speed_mm_s;   /**< Швидкість у кінці сегмента, мм/с. */
    double nominal_speed_mm_s;/**< Номінальна (обмежена feed/лімітами) швидкість, мм/с. */
    double accel_mm_s2;      /**< Використане прискорення/гальмування, мм/с². */
    double accel_distance_mm;/**< Довжина розгону, мм. */
    double cruise_distance_mm;/**< Довжина крейсерської ділянки, мм. */
    double decel_distance_mm;/**< Довжина гальмування, мм. */
    bool pen_down;           /**< Стан пера для сегмента. */
} plan_block_t;

/**
 * @brief Обчислює послідовність блоків руху за заданими сегментами.
 * @param limits Обмеження пристрою (швидкість, прискорення, тощо).
 * @param start_position_mm Початкова позиція (X,Y) у мм; може бути `NULL` (вважається 0,0).
 * @param segments Масив вхідних сегментів.
 * @param segment_count Кількість елементів у `segments`.
 * @param out_blocks [out] Вказівник на масив результатних блоків (виділяється всередині; звільняє викликач через `free`).
 * @param out_count [out] Кількість блоків у масиві `out_blocks`.
 * @return true — успіх; false — помилка параметрів або виділення памʼяті.
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

#endif
