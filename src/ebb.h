/**
 * @file ebb.h
 * @brief Протокол EBB (EiBotBoard) для керування AxiDraw.
 * @defgroup ebb EBB
 * @ingroup device
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
 * @brief Режими мікрокроку для моторів EBB.
 */
typedef enum {
    EBB_MOTOR_DISABLED = 0, /**< Мотор вимкнено. */
    EBB_MOTOR_STEP_16 = 1,  /**< 1/16 мікрокрок. */
    EBB_MOTOR_STEP_8 = 2,   /**< 1/8 мікрокрок. */
    EBB_MOTOR_STEP_4 = 3,   /**< 1/4 мікрокрок. */
    EBB_MOTOR_STEP_2 = 4,   /**< 1/2 мікрокрок. */
    EBB_MOTOR_STEP_FULL = 5 /**< Повний крок. */
} ebb_motor_mode_t;

/**
 * @brief Прапорці очищення лічильників для низькорівневих команд.
 */
typedef enum {
    EBB_CLEAR_NONE = 0,  /**< Не очищати. */
    EBB_CLEAR_AXIS1 = 1, /**< Очистити лічильник осі 1. */
    EBB_CLEAR_AXIS2 = 2, /**< Очистити лічильник осі 2. */
    EBB_CLEAR_BOTH = 3,  /**< Очистити обидва лічильники. */
} ebb_clear_flag_t;

/**
 * @brief Стан руху та черги команд у контролері.
 */
typedef struct {
    int command_active; /**< 1 — виконується команда руху. */
    int motor1_active;  /**< 1 — мотор 1 активний. */
    int motor2_active;  /**< 1 — мотор 2 активний. */
    int fifo_pending;   /**< Кількість команд у черзі FIFO (якщо повідомляється). */
} ebb_motion_status_t;

/**
 * @brief Увімкнути мотори з режимами мікрокроку.
 * @param sp Відкритий порт EBB.
 * @param motor1 Режим мотора 1.
 * @param motor2 Режим мотора 2.
 * @param timeout_ms Тайм-аут команди (мс).
 * @return 0 — успіх, інакше помилка.
 */
int ebb_enable_motors (
    serial_port_t *sp, ebb_motor_mode_t motor1, ebb_motor_mode_t motor2, int timeout_ms);

/**
 * @brief Вимкнути живлення моторів.
 * @param sp Порт EBB.
 * @param timeout_ms Тайм-аут (мс).
 * @return 0 — успіх, інакше помилка.
 */
int ebb_disable_motors (serial_port_t *sp, int timeout_ms);
/**
 * @brief Рух на задану кількість кроків за визначений час.
 * @param sp Порт.
 * @param duration_ms Тривалість інтервалу (мс).
 * @param steps1 Кроки осі 1.
 * @param steps2 Кроки осі 2.
 * @param timeout_ms Тайм-аут (мс).
 * @return 0 — успіх, інакше помилка.
 */
int ebb_move_steps (
    serial_port_t *sp, uint32_t duration_ms, int32_t steps1, int32_t steps2, int timeout_ms);

/**
 * @brief Керує станом пера через сервопривід.
 * @param sp Порт.
 * @param pen_up true — перо вгору, false — вниз.
 * @param settle_ms Затримка стабілізації (мс).
 * @param portb_pin Альтернативний пін (або -1).
 * @param timeout_ms Тайм-аут (мс).
 * @return 0 — успіх, інакше помилка.
 */
int ebb_pen_set (serial_port_t *sp, bool pen_up, int settle_ms, int portb_pin, int timeout_ms);

/**
 * @brief Зручна обгортка для підняття пера (SP,1).
 * @param sp Порт EBB.
 * @param timeout_ms Тайм-аут команди (мс).
 * @return 0 — успіх, інакше помилка.
 */
static inline int ebb_pen_up (serial_port_t *sp, int timeout_ms) {
    return ebb_pen_set (sp, true, 0, -1, timeout_ms);
}

static inline int ebb_pen_down (serial_port_t *sp, int timeout_ms) {
    return ebb_pen_set (sp, false, 0, -1, timeout_ms);
}

/**
 * @brief Рух у CoreXY-просторі (A/B осі) на задані кроки.
 * @param sp Порт.
 * @param duration_ms Тривалість (мс).
 * @param steps_a Кроки уздовж осі A.
 * @param steps_b Кроки уздовж осі B.
 * @param timeout_ms Тайм-аут (мс).
 * @return 0 — успіх, інакше помилка.
 */
int ebb_move_mixed (
    serial_port_t *sp, uint32_t duration_ms, int32_t steps_a, int32_t steps_b, int timeout_ms);

/**
 * @brief Низькорівневий рух із швидкостями/прискореннями та кроками.
 * @param sp Порт.
 * @param rate1 Початкова швидкість осі 1.
 * @param steps1 Кроки осі 1.
 * @param accel1 Прискорення осі 1.
 * @param rate2 Початкова швидкість осі 2.
 * @param steps2 Кроки осі 2.
 * @param accel2 Прискорення осі 2.
 * @param clear_flags Прапори очищення (або -1 — без параметра).
 * @param timeout_ms Тайм-аут (мс).
 * @return 0 — успіх, інакше помилка.
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
 * @brief Низькорівневий рух із фіксованою кількістю інтервалів.
 * @param sp Порт.
 * @param intervals Кількість інтервалів.
 * @param rate1 Швидкість осі 1.
 * @param accel1 Прискорення осі 1.
 * @param rate2 Швидкість осі 2.
 * @param accel2 Прискорення осі 2.
 * @param clear_flags Прапори очищення (або -1 — без параметра).
 * @param timeout_ms Тайм-аут (мс).
 * @return 0 — успіх, інакше помилка.
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
 * @brief Хоумінг: рух до заданих позицій із кроковою швидкістю.
 * @param sp Порт.
 * @param step_rate Частота кроків.
 * @param pos1 Ціль осі 1 (або NULL).
 * @param pos2 Ціль осі 2 (або NULL).
 * @param timeout_ms Тайм-аут (мс).
 * @return 0 — успіх, інакше помилка.
 */
int ebb_home_move (
    serial_port_t *sp,
    uint32_t step_rate,
    const int32_t *pos1,
    const int32_t *pos2,
    int timeout_ms);

/**
 * @brief Зчитує лічильники кроків осей.
 * @param sp Порт.
 * @param steps1_out [out] Кроки осі 1.
 * @param steps2_out [out] Кроки осі 2.
 * @param timeout_ms Тайм-аут (мс).
 * @return 0 — успіх, інакше помилка.
 */
int ebb_query_steps (serial_port_t *sp, int32_t *steps1_out, int32_t *steps2_out, int timeout_ms);

/**
 * @brief Обнуляє лічильники кроків осей.
 * @param sp Порт.
 * @param timeout_ms Тайм-аут (мс).
 * @return 0 — успіх, інакше помилка.
 */
int ebb_clear_steps (serial_port_t *sp, int timeout_ms);

/**
 * @brief Запит статусу руху (активність/черга).
 * @param sp Порт.
 * @param status_out [out] Структура стану (QM).
 * @param timeout_ms Тайм-аут (мс).
 * @return 0 — успіх, інакше помилка.
 */
int ebb_query_motion (serial_port_t *sp, ebb_motion_status_t *status_out, int timeout_ms);

/**
 * @brief Запит стану пера.
 * @param sp Порт.
 * @param pen_up_out [out] true — перо вгору.
 * @param timeout_ms Тайм-аут (мс).
 * @return 0 — успіх, інакше помилка.
 */
int ebb_query_pen (serial_port_t *sp, bool *pen_up_out, int timeout_ms);

/**
 * @brief Запит живлення сервоприводу.
 * @param sp Порт.
 * @param power_on_out [out] true — живлення увімкнено.
 * @param timeout_ms Тайм-аут (мс).
 * @return 0 — успіх, інакше помилка.
 */
int ebb_query_servo_power (serial_port_t *sp, bool *power_on_out, int timeout_ms);

/**
 * @brief Зчитує рядок версії прошивки.
 * @param sp Порт.
 * @param version_buf [out] Буфер для рядка версії.
 * @param version_len Довжина буфера версії.
 * @param timeout_ms Тайм-аут (мс).
 * @return 0 — успіх, інакше помилка.
 */
int ebb_query_version (serial_port_t *sp, char *version_buf, size_t version_len, int timeout_ms);

/**
 * @brief Налаштовує параметр режиму контролера.
 * @param sp Порт.
 * @param param_id Ідентифікатор параметра (0..255).
 * @param value Значення (0..65535).
 * @param timeout_ms Тайм-аут (мс).
 * @return 0 — успіх, інакше помилка.
 */
int ebb_configure_mode (serial_port_t *sp, int param_id, int value, int timeout_ms);

/**
 * @brief Встановлює тайм-аут живлення серво та стан.
 * @param sp Порт.
 * @param timeout_ms Тайм-аут у мілісекундах.
 * @param power_state 0/1 для вимкнення/увімкнення (інше — не змінювати).
 * @param cmd_timeout_ms Тайм-аут команди (мс).
 * @return 0 — успіх, інакше помилка.
 */
int ebb_set_servo_power_timeout (
    serial_port_t *sp, uint32_t timeout_ms, int power_state, int cmd_timeout_ms);

/**
 * @brief Аварійна зупинка контролера.
 * @param sp Порт.
 * @param timeout_ms Тайм-аут (мс).
 * @return 0 — успіх, інакше помилка.
 */
int ebb_emergency_stop (serial_port_t *sp, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
