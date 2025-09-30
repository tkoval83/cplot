/**
 * @file axidraw.h
 * @brief Взаємодія з пристроєм AxiDraw та профілі/налаштування.
 * @defgroup axidraw AxiDraw
 * @ingroup device
 */
/**
 * @defgroup device Пристрій
 * Узагальнююча група для взаємодії з апаратурою: AxiDraw, EBB, Serial.
 */
#ifndef AXIDRAW_H
#define AXIDRAW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "config.h"
#include "serial.h"

/** Повідомлення про відсутній порт у конфігурації. */
#define AXIDRAW_ERR_PORT_NOT_SPECIFIED "Не вказано порт AxiDraw"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Профіль пристрою (модель і номінальні параметри).
 */
typedef struct {
    const char *model;
    double paper_w_mm;
    double paper_h_mm;
    double speed_mm_s;
    double accel_mm_s2;
    double steps_per_mm;
} axidraw_device_profile_t;

/** Повертає типовий профіль пристрою. */
const axidraw_device_profile_t *axidraw_device_profile_default (void);

/**
 * @brief Знаходить профіль за назвою моделі.
 * @param model Назва моделі (наприклад, "minikit2").
 * @return Профіль або NULL, якщо не знайдено.
 */
const axidraw_device_profile_t *axidraw_device_profile_for_model (const char *model);

/** Застосовує профіль до конфігурації. */
void axidraw_device_profile_apply (config_t *cfg, const axidraw_device_profile_t *profile);

/** Дозаповнює відсутні значення профілем. */
bool axidraw_device_profile_backfill (config_t *cfg, const axidraw_device_profile_t *profile);

/**
 * @brief Параметри сеансу/прошивки, ліміти FIFO та серво.
 */
typedef struct {
    double min_cmd_interval_ms;
    size_t fifo_limit;
    int pen_up_delay_ms;
    int pen_down_delay_ms;
    int pen_up_pos;
    int pen_down_pos;
    int pen_up_speed;
    int pen_down_speed;
    int servo_timeout_s;
    double speed_mm_s;
    double accel_mm_s2;
    double steps_per_mm;
} axidraw_settings_t;

/**
 * @brief Екземпляр підключеного пристрою AxiDraw.
 */
typedef struct {
    serial_port_t *port;
    char port_path[256];
    int baud;
    int timeout_ms;
    double min_cmd_interval;
    struct timespec last_cmd;
    size_t max_fifo_commands;
    size_t pending_commands;
    axidraw_settings_t settings;
    bool connected;
} axidraw_device_t;

/** Режими мікрокроку моторів AxiDraw. */
typedef enum {
    AXIDRAW_MOTOR_DISABLED = 0,
    AXIDRAW_MOTOR_STEP_16 = 1,
    AXIDRAW_MOTOR_STEP_8 = 2,
    AXIDRAW_MOTOR_STEP_4 = 3,
    AXIDRAW_MOTOR_STEP_2 = 4,
    AXIDRAW_MOTOR_STEP_FULL = 5
} axidraw_motor_mode_t;

/**
 * @brief Стан руху та черги команд (аналог ebb_motion_status_t).
 */
typedef struct {
    bool command_active;
    bool motor1_active;
    bool motor2_active;
    int fifo_pending;
} axidraw_motion_status_t;

/** Скидає налаштування до типових значень. */
void axidraw_settings_reset (axidraw_settings_t *settings);

/** Повертає поточні налаштування пристрою. */
const axidraw_settings_t *axidraw_device_settings (const axidraw_device_t *dev);

/**
 * @brief Застосовує налаштування до пристрою (rate/FIFO/серво).
 * @param dev Пристрій.
 * @param settings Джерело налаштувань.
 */
void axidraw_apply_settings (axidraw_device_t *dev, const axidraw_settings_t *settings);

/** Ініціалізує структуру пристрою. */
void axidraw_device_init (axidraw_device_t *dev);

/**
 * @brief Конфігурує параметри доступу до серійного порту.
 * @param dev Пристрій.
 * @param port_path Шлях до пристрою.
 * @param baud Бодова швидкість.
 * @param timeout_ms Тайм-аут читання (мс).
 * @param min_cmd_interval_ms Мінімальний інтервал між командами (мс).
 */
void axidraw_device_config (
    axidraw_device_t *dev,
    const char *port_path,
    int baud,
    int timeout_ms,
    double min_cmd_interval_ms);

/**
 * @brief Встановлює зʼєднання з контролером EBB.
 * @param dev Пристрій.
 * @param errbuf [out] Буфер помилки.
 * @param errlen Розмір буфера.
 * @return 0 — успіх, інакше помилка.
 */
int axidraw_device_connect (axidraw_device_t *dev, char *errbuf, size_t errlen);

/** Розриває зʼєднання з пристроєм. */
void axidraw_device_disconnect (axidraw_device_t *dev);

/** Захоплює lock-файл для ексклюзивного доступу. */
int axidraw_device_lock_acquire (int *out_fd);

/** Звільняє lock-файл. */
void axidraw_device_lock_release (int fd);

/** Шлях до lock-файлу пристрою. */
const char *axidraw_device_lock_file (void);

/** Негайна зупинка усіх рухів. */
int axidraw_emergency_stop (axidraw_device_t *dev);

/** Перевіряє стан зʼєднання. */
bool axidraw_device_is_connected (const axidraw_device_t *dev);

/** Встановлює мінімальний інтервал між командами (мс). */
void axidraw_set_rate_limit (axidraw_device_t *dev, double min_interval_ms);

/** Обмежує максимальну кількість команд у FIFO. */
void axidraw_set_fifo_limit (axidraw_device_t *dev, size_t max_fifo_commands);

/** Команда підняття пера. */
int axidraw_pen_up (axidraw_device_t *dev);

/** Команда опускання пера. */
int axidraw_pen_down (axidraw_device_t *dev);

/** Встановлює режими мікрокроку моторів. */
int axidraw_motors_set_mode (
    axidraw_device_t *dev, axidraw_motor_mode_t motor1, axidraw_motor_mode_t motor2);

/** Вимикає живлення моторів. */
int axidraw_motors_disable (axidraw_device_t *dev);

/** Зчитує лічильники кроків з контролера. */
int axidraw_query_steps (axidraw_device_t *dev, int32_t *steps1_out, int32_t *steps2_out);

/** Очищає лічильники кроків. */
int axidraw_clear_steps (axidraw_device_t *dev);

/** Запитує стан руху/черги FIFO. */
int axidraw_query_motion (axidraw_device_t *dev, axidraw_motion_status_t *status_out);

/** Повертає стан пера. */
int axidraw_query_pen (axidraw_device_t *dev, bool *pen_up_out);

/** Повертає стан живлення сервоприводу. */
int axidraw_query_servo_power (axidraw_device_t *dev, bool *power_on_out);

/** Зчитує рядок версії прошивки. */
int axidraw_query_version (axidraw_device_t *dev, char *buffer, size_t buffer_len);

/**
 * @brief Рух по декартових осях.
 * @param dev Пристрій.
 * @param duration_ms Тривалість (мс).
 * @param steps_x Кроки по X.
 * @param steps_y Кроки по Y.
 */
int axidraw_move_xy (axidraw_device_t *dev, uint32_t duration_ms, int32_t steps_x, int32_t steps_y);

/** Рух у кінематиці CoreXY. */
int axidraw_move_corexy (
    axidraw_device_t *dev, uint32_t duration_ms, int32_t steps_a, int32_t steps_b);

/**
 * @brief Низькорівнева команда руху зі швидкостями та прискореннями.
 * @param dev Пристрій.
 * @param rate1 Початкова швидкість осі 1 (крок/інтервал).
 * @param steps1 Кроки осі 1.
 * @param accel1 Прискорення осі 1.
 * @param rate2 Початкова швидкість осі 2.
 * @param steps2 Кроки осі 2.
 * @param accel2 Прискорення осі 2.
 * @param clear_flags Прапори очищення FIFO/лічильників.
 */
int axidraw_move_lowlevel (
    axidraw_device_t *dev,
    uint32_t rate1,
    int32_t steps1,
    int32_t accel1,
    uint32_t rate2,
    int32_t steps2,
    int32_t accel2,
    int clear_flags);

/**
 * @brief Низькорівнева команда руху із фіксованою кількістю інтервалів.
 * @param dev Пристрій.
 * @param intervals К-сть інтервалів.
 * @param rate1 Швидкість осі 1.
 * @param accel1 Прискорення осі 1.
 * @param rate2 Швидкість осі 2.
 * @param accel2 Прискорення осі 2.
 * @param clear_flags Прапори очищення.
 */
int axidraw_move_lowlevel_time (
    axidraw_device_t *dev,
    uint32_t intervals,
    int32_t rate1,
    int32_t accel1,
    int32_t rate2,
    int32_t accel2,
    int clear_flags);

/**
 * @brief Повернення у нульові координати (home).
 * @param dev Пристрій.
 * @param step_rate Частота кроків.
 * @param pos1 Цільова позиція осі 1.
 * @param pos2 Цільова позиція осі 2.
 */
int axidraw_home (
    axidraw_device_t *dev, uint32_t step_rate, const int32_t *pos1, const int32_t *pos2);

/**
 * @brief Рух у міліметрах із заданою швидкістю.
 * @param dev Пристрій.
 * @param dx_mm Зсув X (мм).
 * @param dy_mm Зсув Y (мм).
 * @param speed_mm_s Швидкість (мм/с).
 */
int axidraw_move_mm (axidraw_device_t *dev, double dx_mm, double dy_mm, double speed_mm_s);

/**
 * @brief Перетворює відстань у мм на кроки відповідно до профілю пристрою.
 * @param dev Пристрій (джерело steps_per_mm).
 * @param mm Відстань у міліметрах.
 * @return Кількість кроків (насичення до діапазону int32).
 */
int32_t axidraw_mm_to_steps (const axidraw_device_t *dev, double mm);

/**
 * @brief Відправляє одну фазу руху у LowLevel режимі на основі параметрів фази.
 * @param dev Пристрій (має бути підключений).
 * @param distance_mm Довжина фази, мм (використовується для перетворення швидкості у кроки/с).
 * @param start_speed_mm_s Початкова швидкість, мм/с.
 * @param end_speed_mm_s Кінцева швидкість, мм/с.
 * @param steps_a Кроки вздовж осі A у цій фазі.
 * @param steps_b Кроки вздовж осі B у цій фазі.
 * @param duration_s Тривалість фази, секунди.
 * @return 0 — успіх; -1 — помилка або некоректні параметри.
 */
int axidraw_move_lowlevel_phase (
    axidraw_device_t *dev,
    double distance_mm,
    double start_speed_mm_s,
    double end_speed_mm_s,
    int32_t steps_a,
    int32_t steps_b,
    double duration_s);

/**
 * @brief Відправляє фазу руху у координатах X/Y (CoreXY мапінг всередині).
 * @param dev Пристрій.
 * @param distance_mm Довжина фази, мм.
 * @param start_speed_mm_s Початкова швидкість, мм/с.
 * @param end_speed_mm_s Кінцева швидкість, мм/с.
 * @param steps_x Кроки по осі X у фазі.
 * @param steps_y Кроки по осі Y у фазі.
 * @param duration_s Тривалість фази, с.
 * @return 0 — успіх; -1 — помилка.
 */
int axidraw_move_lowlevel_phase_xy (
    axidraw_device_t *dev,
    double distance_mm,
    double start_speed_mm_s,
    double end_speed_mm_s,
    int32_t steps_x,
    int32_t steps_y,
    double duration_s);

/**
 * @brief Очікує, доки пристрій стане неактивним (без рухів/команд у FIFO).
 * @param dev Підключений пристрій.
 * @param max_attempts Кількість ітерацій очікування (крок ~20 мс).
 * @return 0 — пристрій простійний; -1 — тайм-аут або помилка запиту.
 */
int axidraw_wait_for_idle (axidraw_device_t *dev, int max_attempts);

/**
 * @brief Виконує повернення у базову позицію з безпечними швидкісними параметрами.
 * @param dev Підключений пристрій.
 * @return 0 — успіх; -1 — помилка параметрів/виконання.
 */
int axidraw_home_default (axidraw_device_t *dev);

#ifdef __cplusplus
}
#endif

#endif
