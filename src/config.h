/**
 * @file config.h
 * @brief Постійна конфігурація для параметрів пристрою та розкладки.
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stddef.h>

/// Орієнтація сторінки (узгоджено з CLI).
typedef enum {
    ORIENT_PORTRAIT = 1, /**< портретна орієнтація */
    ORIENT_LANDSCAPE = 2 /**< альбомна орієнтація */
} orientation_t;

/// Конфігураційний блок для параметрів пристрою/розкладки (типово MiniKit2).
typedef struct {
    // Версіонування
    int version; /**< інкремент при зміні формату на диску */

    // Сторінка та розміщення
    orientation_t orientation; /**< орієнтація сторінки */
    double paper_w_mm;
    double paper_h_mm;
    double margin_top_mm;
    double margin_right_mm;
    double margin_bottom_mm;
    double margin_left_mm;
    double font_size_pt;   /**< Базовий кегль шрифту (pt). */
    char font_family[128]; /**< Типова родина шрифтів. */

    // Рух
    double speed_mm_s;  /**< номінальна швидкість */
    double accel_mm_s2; /**< номінальне прискорення */

    // Серво пера
    int pen_up_pos;        /**< 0..100 відсотків */
    int pen_down_pos;      /**< 0..100 відсотків */
    int pen_up_speed;      /**< відсотків/с */
    int pen_down_speed;    /**< відсотків/с */
    int pen_up_delay_ms;   /**< затримка після підйому пера */
    int pen_down_delay_ms; /**< затримка після опускання пера */
    int servo_timeout_s;   /**< 0 вимикає авто-вимкнення; типово 60 */

    // Обраний віддалений пристрій (псевдонім із `device list`).
    char default_device[64];
} config_t;

/** Типова модель пристрою, яку використовує CLI. */
#define CONFIG_DEFAULT_MODEL "minikit2"

/** Типова родина шрифтів (ідентифікатор у реєстрі Hershey). */
#define CONFIG_DEFAULT_FONT_FAMILY "EMS Nixish"

/**
 * Заповнити конфігурацію типовими (CLI) заводськими налаштуваннями.
 *
 * @param c            Структура конфігурації для ініціалізації (не NULL).
 * @param device_model Залишено для сумісності; пристрій має бути обраний окремо на сервері.
 * @return 0 при успіху; -1 при некоректних аргументах.
 */
int config_factory_defaults (config_t *c, const char *device_model);

/**
 * Завантажити активну конфігурацію з диска, якщо є; інакше застосувати заводські значення.
 * @param out Вихідна конфігурація для заповнення (не NULL).
 * @return 0 у разі успіху; ненульове при помилках I/O/парсингу (дефолти все одно застосовано).
 */
int config_load (config_t *out);

/**
 * Зберегти конфігурацію на диск атомарно.
 * @param cfg Конфігурація для збереження.
 * @return 0 у разі успіху; ненульове при помилці.
 */
int config_save (const config_t *cfg);

/**
 * Скинути конфігурацію до заводських налаштувань і зберегти.
 * @return 0 у разі успіху; ненульове при помилці.
 */
int config_reset (void);

/**
 * Валідвати діапазони налаштувань.
 * @param c      Конфіг для перевірки.
 * @param err    Необов'язковий буфер для короткого повідомлення.
 * @param errlen Розмір буфера err.
 * @return 0 якщо валідно; інакше ненульове.
 */
int config_validate (const config_t *c, char *err, size_t errlen);

/**
 * Визначити шлях до файлу конфігурації у buf.
 * @param buf     Вихідний буфер для шляху.
 * @param buflen  Розмір буфера.
 * @return 0 у разі успіху; ненульове при помилці.
 */
int config_get_path (char *buf, size_t buflen);

#endif /* CONFIG_H */
