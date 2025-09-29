/**
 * @file ebb.h
 * @brief Високорівневі обгортки над протоколом EBB (EiBotBoard).
 */
#ifndef EBB_H
#define EBB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "serial.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Режими ввімкнення моторів для команди EM.
 * Значення відповідають параметрам Enable1/Enable2 у документації EBB.
 */
typedef enum {
    EBB_MOTOR_DISABLED = 0, /**< Вимкнути мотор (вільне обертання). */
    EBB_MOTOR_STEP_16 = 1,  /**< 1/16 мікрокрок, мотор увімкнено. */
    EBB_MOTOR_STEP_8 = 2,   /**< 1/8 мікрокрок, мотор увімкнено. */
    EBB_MOTOR_STEP_4 = 3,   /**< 1/4 мікрокрок, мотор увімкнено. */
    EBB_MOTOR_STEP_2 = 4,   /**< 1/2 мікрокрок, мотор увімкнено. */
    EBB_MOTOR_STEP_FULL = 5 /**< Повний крок, мотор увімкнено. */
} ebb_motor_mode_t;

typedef enum {
    EBB_CLEAR_NONE = 0,  /**< Не скидати жодного акумулятора. */
    EBB_CLEAR_AXIS1 = 1, /**< Скинути акумулятор осі 1 (LM/LT). */
    EBB_CLEAR_AXIS2 = 2, /**< Скинути акумулятор осі 2 (LM/LT). */
    EBB_CLEAR_BOTH = 3,  /**< Скинути обидва акумулятори. */
} ebb_clear_flag_t;

/**
 * Статус рухової системи, повернутий командою QM.
 */
typedef struct {
    int command_active; /**< 1 якщо виконується рухова команда. */
    int motor1_active;  /**< 1 якщо мотор 1 рухається. */
    int motor2_active;  /**< 1 якщо мотор 2 рухається. */
    int fifo_pending;   /**< 1 якщо FIFO не порожній (версії ≥ 2.4.4). */
} ebb_motion_status_t;

/* Агрегований знімок стану вилучено; для отримання інформації використовуйте
 * окремі виклики ebb_query_motion/steps/pen/servo_power/version. */

/**
 * Надіслати команду EM для ввімкнення/вимкнення моторів та вибору мікрокроку.
 *
 * @param sp        Відкритий послідовний порт EBB.
 * @param motor1    Режим для мотора 1 (визначає глобальний мікрокрок).
 * @param motor2    Режим для мотора 2 (0 = вимкнено, інше = увімкнено).
 * @param timeout_ms Тайм-аут очікування відповіді OK.
 * @return 0 при успіху; -1 при помилці або негативній відповіді.
 */
int ebb_enable_motors (
    serial_port_t *sp, ebb_motor_mode_t motor1, ebb_motor_mode_t motor2, int timeout_ms);

/**
 * Псевдонім для EM,0,0 — повністю вимкнути мотори.
 *
 * @param sp        Відкритий порт.
 * @param timeout_ms Тайм-аут очікування відповіді.
 * @return 0 при успіху; -1 при помилці.
 */
int ebb_disable_motors (serial_port_t *sp, int timeout_ms);

/**
 * Виконати прямолінійний рух (SM) із зазначеною тривалістю та кроками.
 *
 * @param sp          Відкритий послідовний порт.
 * @param duration_ms Тривалість у мілісекундах (1..16777215).
 * @param steps1      Кроки по осі 1 (-16777215..16777215).
 * @param steps2      Кроки по осі 2 (-16777215..16777215).
 * @param timeout_ms  Тайм-аут очікування відповіді.
 * @return 0 при успіху; -1 при помилці або хибних аргументах.
 */
int ebb_move_steps (
    serial_port_t *sp, uint32_t duration_ms, int32_t steps1, int32_t steps2, int timeout_ms);

/**
 * Перемістити перо (SP) із можливим додатковим очікуванням.
 *
 * @param sp          Відкритий послідовний порт.
 * @param pen_up      true → підняти перо (SP,1), false → опустити (SP,0).
 * @param settle_ms   Додаткова пауза перед наступною командою (0..65535).
 * @param portb_pin   Номер RB-виводу (0..7) або -1 для значення за замовчуванням.
 * @param timeout_ms  Тайм-аут очікування відповіді.
 * @return 0 при успіху; -1 при помилці/хибних аргументах.
 */
int ebb_pen_set (serial_port_t *sp, bool pen_up, int settle_ms, int portb_pin, int timeout_ms);

/** @brief Підняти перо (SP,1) без додаткових параметрів. */
static inline int ebb_pen_up (serial_port_t *sp, int timeout_ms) {
    return ebb_pen_set (sp, true, 0, -1, timeout_ms);
}

/** @brief Опустити перо (SP,0) без додаткових параметрів. */
static inline int ebb_pen_down (serial_port_t *sp, int timeout_ms) {
    return ebb_pen_set (sp, false, 0, -1, timeout_ms);
}

/**
 * Зробити рух у координатах A/B (XM) для CoreXY/H-bot кінематики.
 *
 * @param sp          Відкритий послідовний порт.
 * @param duration_ms Тривалість у мілісекундах (1..16777215).
 * @param steps_a     Кроки вздовж осі A (-16777215..16777215).
 * @param steps_b     Кроки вздовж осі B (-16777215..16777215).
 * @param timeout_ms  Тайм-аут очікування OK.
 * @return 0 у разі успіху; -1 при помилці або некоректних параметрах.
 */
int ebb_move_mixed (
    serial_port_t *sp, uint32_t duration_ms, int32_t steps_a, int32_t steps_b, int timeout_ms);

/**
 * Низькорівнева команда LM (step-limited) з можливістю задавати прискорення.
 *
 * @param sp          Відкритий послідовний порт.
 * @param rate1       Початкова швидкість осі 1 (0..2147483647).
 * @param steps1      Цільові кроки осі 1 (±2147483647).
 * @param accel1      Прискорення осі 1 (±2147483647).
 * @param rate2       Початкова швидкість осі 2 (0..2147483647).
 * @param steps2      Цільові кроки осі 2 (±2147483647).
 * @param accel2      Прискорення осі 2 (±2147483647).
 * @param clear_flags Значення EBB_CLEAR_* або -1 щоб не передавати параметр.
 * @param timeout_ms  Тайм-аут очікування OK.
 * @return 0 у разі успіху; -1 при помилці/хибних параметрах.
 */
int ebb_move_lowlevel_steps (
    serial_port_t *sp,
    uint32_t rate1,
    int32_t steps1,
    int32_t accel1,
    uint32_t rate2,
    int32_t steps2,
    int32_t accel2,
    int clear_flags,
    int timeout_ms);

/**
 * Низькорівнева команда LT (time-limited) з можливістю задавати прискорення.
 *
 * @param sp          Відкритий послідовний порт.
 * @param intervals   Тривалість у 40-µs інтервалах (>0).
 * @param rate1       Початкова швидкість осі 1 (±2147483647).
 * @param accel1      Прискорення осі 1 (±2147483647).
 * @param rate2       Початкова швидкість осі 2 (±2147483647).
 * @param accel2      Прискорення осі 2 (±2147483647).
 * @param clear_flags Значення EBB_CLEAR_* або -1 щоб не передавати параметр.
 * @param timeout_ms  Тайм-аут очікування OK.
 * @return 0 у разі успіху; -1 при помилці/хибних параметрах.
 */
int ebb_move_lowlevel_time (
    serial_port_t *sp,
    uint32_t intervals,
    int32_t rate1,
    int32_t accel1,
    int32_t rate2,
    int32_t accel2,
    int clear_flags,
    int timeout_ms);

/**
 * Виконати команду HM — повернення в home або у вказані абсолютні координати.
 *
 * @param sp         Відкритий послідовний порт.
 * @param step_rate  Швидкість у кроках за секунду (2..25000).
 * @param pos1       Якщо не NULL, абсолютна позиція осі 1 (±4'294'967).
 * @param pos2       Якщо не NULL, абсолютна позиція осі 2 (±4'294'967).
 * @param timeout_ms Тайм-аут очікування OK.
 * @return 0 у разі успіху; -1 при помилці або хибних параметрах.
 */
int ebb_home_move (
    serial_port_t *sp,
    uint32_t step_rate,
    const int32_t *pos1,
    const int32_t *pos2,
    int timeout_ms);

/**
 * Отримати глобальні лічильники кроків (QS).
 *
 * @param sp         Відкритий послідовний порт.
 * @param steps1_out Вихід для позиції осі 1 (не NULL).
 * @param steps2_out Вихід для позиції осі 2 (не NULL).
 * @param timeout_ms Тайм-аут очікування відповіді.
 * @return 0 при успіху; -1 при помилці/некоректних даних.
 */
int ebb_query_steps (serial_port_t *sp, int32_t *steps1_out, int32_t *steps2_out, int timeout_ms);

/**
 * Обнулити глобальні лічильники кроків (CS).
 *
 * @param sp         Відкритий послідовний порт.
 * @param timeout_ms Тайм-аут очікування OK.
 * @return 0 при успіху; -1 при помилці.
 */
int ebb_clear_steps (serial_port_t *sp, int timeout_ms);

/**
 * Отримати статус рухової системи (QM).
 *
 * @param sp         Відкритий послідовний порт.
 * @param status_out Вихідна структура (не NULL).
 * @param timeout_ms Тайм-аут очікування відповіді.
 * @return 0 при успіху; -1 при помилці/некоректних даних.
 */
int ebb_query_motion (serial_port_t *sp, ebb_motion_status_t *status_out, int timeout_ms);

/**
 * Дізнатись стан пера (QP).
 *
 * @param sp         Відкритий послідовний порт.
 * @param pen_up_out Вихід: true якщо перо підняте (не NULL).
 * @param timeout_ms Тайм-аут очікування відповіді.
 * @return 0 при успіху; -1 при помилці/некоректних даних.
 */
int ebb_query_pen (serial_port_t *sp, bool *pen_up_out, int timeout_ms);

/**
 * Дізнатись чи подається живлення на сервопривід (QR).
 */
int ebb_query_servo_power (serial_port_t *sp, bool *power_on_out, int timeout_ms);

/**
 * Отримати рядок версії/нікнейму (V або QT).
 */
int ebb_query_version (serial_port_t *sp, char *version_buf, size_t version_len, int timeout_ms);

/**
 * Зібрати агрегований знімок стану (QM, QS, QP, QR, V).
 */
/* ebb_collect_status() вилучено. */

/**
 * @brief Налаштувати параметр режиму (команда SC).
 *
 * @param sp          Відкритий послідовний порт.
 * @param param_id    Ідентифікатор параметра (0..255).
 * @param value       Значення параметра (0..65535).
 * @param timeout_ms  Тайм-аут очікування відповіді.
 * @return 0 при успіху; -1 при помилці або хибних аргументах.
 */
int ebb_configure_mode (serial_port_t *sp, int param_id, int value, int timeout_ms);

/**
 * @brief Встановити тайм-аут живлення сервоприводу (команда SR).
 *
 * @param sp          Відкритий послідовний порт.
 * @param timeout_ms  Тайм-аут авто-вимкнення сервоприводу у мс.
 * @param power_state -1, щоб не змінювати стан живлення; 0 → вимкнути; 1 → увімкнути.
 * @param cmd_timeout_ms Тайм-аут очікування відповіді на команду.
 * @return 0 при успіху; -1 при помилці або хибних аргументах.
 */
int ebb_set_servo_power_timeout (
    serial_port_t *sp, uint32_t timeout_ms, int power_state, int cmd_timeout_ms);

/**
 * @brief Аварійно зупинити всі рухи (команда ES).
 *
 * @param sp         Відкритий послідовний порт.
 * @param timeout_ms Тайм-аут очікування відповіді.
 * @return 0 при успіху; -1 при помилці.
 */
int ebb_emergency_stop (serial_port_t *sp, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* EBB_H */
