/**
 * @file axidraw.h
 * @brief Високорівневий менеджер AxiDraw поверх протоколу EBB.
 */
#ifndef AXIDRAW_H
#define AXIDRAW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "config.h"
#include "ebb.h"
#include "serial.h"

/* Коефіцієнт кроків на міліметр задається у axidraw_device_profile_t. */

/** Загальне повідомлення про відсутність порту пристрою. */
#define AXIDRAW_ERR_PORT_NOT_SPECIFIED "Не вказано порт AxiDraw"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Апаратний профіль моделі AxiDraw (робоче поле та характеристики руху).
 */
typedef struct {
    const char *model;   /**< Ідентифікатор моделі (регістр ігнорується). */
    double paper_w_mm;   /**< Ширина робочого поля у мм. */
    double paper_h_mm;   /**< Висота робочого поля у мм. */
    double speed_mm_s;   /**< Типова швидкість переміщення у мм/с. */
    double accel_mm_s2;  /**< Типове прискорення у мм/с². */
    double steps_per_mm; /**< Кількість кроків (із мікрокроками) на один мм для цієї механіки. */
} axidraw_device_profile_t;

const axidraw_device_profile_t *axidraw_device_profile_default (void);
const axidraw_device_profile_t *axidraw_device_profile_for_model (const char *model);
void axidraw_device_profile_apply (config_t *cfg, const axidraw_device_profile_t *profile);
bool axidraw_device_profile_backfill (config_t *cfg, const axidraw_device_profile_t *profile);

/**
 * @brief Параметри попередньої конфігурації AxiDraw.
 *
 * Ця структура відображає налаштування, що впливають на поведінку пристрою
 * (затримки пера, обмеження швидкості команд, тайм-аути сервоприводу тощо).
 * Вона заповнюється на рівні CLI/конфігурації і передається у модуль axidraw
 * перед встановленням з’єднання.
 */
typedef struct {
    double min_cmd_interval_ms; /**< Мінімальна пауза між командами (мс). */
    size_t fifo_limit;          /**< Обмеження на кількість команд у FIFO (0 = без обмеження). */
    int pen_up_delay_ms;        /**< Затримка після підйому пера (мс). */
    int pen_down_delay_ms;      /**< Затримка після опускання пера (мс). */
    int pen_up_pos;             /**< Положення пера у режимі "вгору" (0..100 або -1). */
    int pen_down_pos;           /**< Положення пера у режимі "вниз" (0..100 або -1). */
    int pen_up_speed;           /**< Швидкість руху пера вгору (од./с або -1). */
    int pen_down_speed;         /**< Швидкість руху пера вниз (од./с або -1). */
    int servo_timeout_s;        /**< Час авто-вимкнення сервоприводу (секунди або -1). */
    double speed_mm_s;          /**< Робоча швидкість переміщення (мм/с). */
    double accel_mm_s2;         /**< Робоче прискорення (мм/с²). */
    double steps_per_mm;        /**< Кроків на мм (від профілю; може бути перевизначено). */
} axidraw_settings_t;

/**
 * Представлення підключеного пристрою AxiDraw.
 */
typedef struct {
    serial_port_t *port;         /**< Внутрішній дескриптор послідовного порту. */
    char port_path[256];         /**< Шлях до tty-порту. */
    int baud;                    /**< Швидкість обміну. */
    int timeout_ms;              /**< Базовий тайм-аут для команд. */
    double min_cmd_interval;     /**< Мінімальний інтервал між командами (мс). */
    struct timespec last_cmd;    /**< Час відправлення останньої команди. */
    size_t max_fifo_commands;    /**< Максимальна кількість команд, допущених у черзі EBB. */
    size_t pending_commands;     /**< Команди, що вже надіслані та очікують виконання. */
    axidraw_settings_t settings; /**< Копія застосованих налаштувань. */
    bool connected;              /**< Статус підключення. */
} axidraw_device_t;

/**
 * @brief Заповнити структуру налаштувань типовими значеннями.
 *
 * @param settings Структура для ініціалізації (не NULL).
 */
void axidraw_settings_reset (axidraw_settings_t *settings);

/**
 * @brief Отримати застосовані налаштування пристрою.
 *
 * @param dev Структура пристрою.
 * @return Вказівник на внутрішні налаштування або NULL.
 */
const axidraw_settings_t *axidraw_device_settings (const axidraw_device_t *dev);

/**
 * @brief Застосувати набір налаштувань до пристрою.
 *
 * Після виклику ліміти швидкості/FIFO синхронізуються з settings.
 *
 * @param dev      Структура пристрою.
 * @param settings Налаштування для копіювання (не NULL).
 */
void axidraw_apply_settings (axidraw_device_t *dev, const axidraw_settings_t *settings);

/**
 * @brief Скинути структуру до стану за замовчуванням.
 *
 * @param dev Структура для ініціалізації (не NULL).
 */
void axidraw_device_init (axidraw_device_t *dev);

/**
 * @brief Налаштувати параметри з’єднання і rate limiter.
 *
 * @param dev Структура пристрою.
 * @param port_path Шлях до tty-порту або NULL.
 * @param baud Швидкість обміну (наприклад 9600).
 * @param timeout_ms Базовий тайм-аут для команд (мс).
 * @param min_cmd_interval_ms Мінімальний інтервал між командами (мс).
 */
void axidraw_device_config (
    axidraw_device_t *dev,
    const char *port_path,
    int baud,
    int timeout_ms,
    double min_cmd_interval_ms);

/**
 * @brief Підключитись до AxiDraw.
 *
 * @param dev Структура пристрою.
 * @param errbuf Буфер для тексту помилки (може бути NULL).
 * @param errlen Розмір буфера errbuf.
 * @return 0 при успіху; -1 при помилці.
 */
int axidraw_device_connect (axidraw_device_t *dev, char *errbuf, size_t errlen);

/**
 * @brief Відключитись від AxiDraw.
 *
 * @param dev Структура пристрою.
 */
void axidraw_device_disconnect (axidraw_device_t *dev);

/**
 * @brief Отримати взаємне виключення доступу до пристрою через lock-файл.
 *
 * @param out_fd Вихід: файловий дескриптор lock-файла.
 * @return 0 при успіху; -1 якщо ресурс зайнятий або сталася помилка.
 */
int axidraw_device_lock_acquire (int *out_fd);

/**
 * @brief Зняти взаємне виключення, отримане через axidraw_device_lock_acquire().
 *
 * @param fd Файловий дескриптор lock-файла.
 */
void axidraw_device_lock_release (int fd);

/**
 * @brief Повернути шлях до lock-файла, який використовує менеджер AxiDraw.
 *
 * @return Вказівник на нуль-термінований рядок.
 */
const char *axidraw_device_lock_file (void);

/**
 * @brief Аварійно зупинити всі рухи пристрою.
 *
 * @param dev Структура пристрою.
 * @return 0 при успіху; -1 при помилці/відсутності з’єднання.
 */
int axidraw_emergency_stop (axidraw_device_t *dev);

/**
 * @brief Перевірити стан з’єднання.
 *
 * @param dev Структура пристрою.
 * @return true, якщо з’єднаний.
 */
bool axidraw_device_is_connected (const axidraw_device_t *dev);

/**
 * @brief Встановити мінімальний інтервал між командами (rate limiter).
 *
 * @param dev Структура пристрою.
 * @param min_interval_ms Інтервал у мс (>= 0).
 */
void axidraw_set_rate_limit (axidraw_device_t *dev, double min_interval_ms);

/**
 * @brief Обмежити кількість одночасних команд у FIFO EBB.
 *
 * @param dev              Структура пристрою.
 * @param max_fifo_commands Максимальна кількість команд (0 → без обмеження).
 */
void axidraw_set_fifo_limit (axidraw_device_t *dev, size_t max_fifo_commands);

/**
 * @brief Підняти перо (SP,1).
 *
 * @param dev Структура пристрою.
 * @return 0 при успіху; -1 при помилці/відсутності з’єднання.
 */
int axidraw_pen_up (axidraw_device_t *dev);

/**
 * @brief Опустити перо (SP,0).
 *
 * @param dev Структура пристрою.
 * @return 0 при успіху; -1 при помилці/відсутності з’єднання.
 */
int axidraw_pen_down (axidraw_device_t *dev);

/**
 * @brief Виконати лінійний рух SM у координатах X/Y.
 *
 * @param dev         Структура пристрою.
 * @param duration_ms Тривалість у мілісекундах.
 * @param steps_x     Кроки для осі X.
 * @param steps_y     Кроки для осі Y.
 * @return 0 при успіху; -1 при помилці.
 */
int axidraw_move_xy (axidraw_device_t *dev, uint32_t duration_ms, int32_t steps_x, int32_t steps_y);

/**
 * @brief Виконати рух у кінематиці CoreXY/H-bot (XM).
 *
 * @param dev         Структура пристрою.
 * @param duration_ms Тривалість у мілісекундах.
 * @param steps_a     Кроки вздовж A.
 * @param steps_b     Кроки вздовж B.
 * @return 0 при успіху; -1 при помилці.
 */
int axidraw_move_corexy (
    axidraw_device_t *dev, uint32_t duration_ms, int32_t steps_a, int32_t steps_b);

/**
 * @brief Виконати низькорівневу команду LM.
 *
 * @param dev        Структура пристрою.
 * @param rate1      Початковий rate осі 1.
 * @param steps1     Кроки осі 1.
 * @param accel1     Прискорення осі 1.
 * @param rate2      Початковий rate осі 2.
 * @param steps2     Кроки осі 2.
 * @param accel2     Прискорення осі 2.
 * @param clear_flags Прапори очищення або -1.
 * @return 0 при успіху; -1 при помилці.
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
 * @brief Виконати низькорівневу команду LT.
 *
 * @param dev        Структура пристрою.
 * @param intervals  Тривалість у 40-µs інтервалах.
 * @param rate1      Початковий rate осі 1.
 * @param accel1     Прискорення осі 1.
 * @param rate2      Початковий rate осі 2.
 * @param accel2     Прискорення осі 2.
 * @param clear_flags Прапори очищення або -1.
 * @return 0 при успіху; -1 при помилці.
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
 * @brief Виконати команду HM (home/абсолютний рух).
 *
 * @param dev       Структура пристрою.
 * @param step_rate Швидкість кроків (2..25000).
 * @param pos1      Абсолютна позиція осі 1 (або NULL).
 * @param pos2      Абсолютна позиція осі 2 (або NULL).
 * @return 0 при успіху; -1 при помилці.
 */
int axidraw_home (
    axidraw_device_t *dev, uint32_t step_rate, const int32_t *pos1, const int32_t *pos2);

/**
 * @brief Зібрати агрегований статус пристрою.
 *
 * @param dev      Структура пристрою.
 * @param snapshot Вихідна структура (не NULL).
 * @return 0 при успіху; -1 при помилці.
 */
int axidraw_status (axidraw_device_t *dev, ebb_status_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif /* AXIDRAW_H */
