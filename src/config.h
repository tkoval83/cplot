/**
 * @file config.h
 * @brief Зберігання та валідація конфігурації застосунку без сторонніх бібліотек.
 * @defgroup config Конфігурація
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Орієнтація сторінки при друці.
 */
typedef enum {
    ORIENT_PORTRAIT = 1, /**< Портретна орієнтація. */
    ORIENT_LANDSCAPE = 2 /**< Альбомна орієнтація. */
} orientation_t;

/**
 * @brief Структура конфігурації користувача.
 */
typedef struct {
    int version; /**< Версія формату конфігурації. */

    orientation_t orientation; /**< Орієнтація сторінки. */
    double paper_w_mm;         /**< Ширина паперу, мм. */
    double paper_h_mm;         /**< Висота паперу, мм. */
    double margin_top_mm;      /**< Верхнє поле, мм. */
    double margin_right_mm;    /**< Праве поле, мм. */
    double margin_bottom_mm;   /**< Нижнє поле, мм. */
    double margin_left_mm;     /**< Ліве поле, мм. */
    double font_size_pt;       /**< Розмір шрифту, пт. */
    char font_family[128];     /**< Типова шрифтна родина. */

    double speed_mm_s;  /**< Швидкість руху, мм/с. */
    double accel_mm_s2; /**< Прискорення, мм/с^2. */

    int pen_up_pos;        /**< Положення пера вгору (%, 0..100). */
    int pen_down_pos;      /**< Положення пера вниз (%, 0..100). */
    int pen_up_speed;      /**< Швидкість підняття пера (умовн. од.). */
    int pen_down_speed;    /**< Швидкість опускання пера (умовн. од.). */
    int pen_up_delay_ms;   /**< Затримка після підняття пера, мс. */
    int pen_down_delay_ms; /**< Затримка після опускання пера, мс. */
    int servo_timeout_s;   /**< Тайм-аут живлення серво, с. */

    char default_device[64]; /**< Типовий псевдонім пристрою. */
} config_t;

/** Типова модель пристрою. */
#define CONFIG_DEFAULT_MODEL "minikit2"

/** Типова шрифтна родина. */
#define CONFIG_DEFAULT_FONT_FAMILY "EMS Nixish"

/**
 * @brief Заповнює конфіг значеннями за замовчуванням.
 * @param c [out] Місце призначення.
 * @param device_model Профіль моделі для ініціалізації (NULL — типова).
 * @return 0 — успіх, -1 — помилка.
 */
int config_factory_defaults (config_t *c, const char *device_model);

/**
 * @brief Завантажує конфігурацію з XDG-шляху.
 * @param out [out] Заповнена конфігурація.
 * @return 0 — успіх, <0 — код помилки.
 */
int config_load (config_t *out);

/**
 * @brief Зберігає конфігурацію на диск (JSON).
 * @param cfg Конфігурація для запису.
 * @return 0 — успіх, <0 — код помилки.
 */
int config_save (const config_t *cfg);

/**
 * @brief Скидає конфігурацію до типових значень і зберігає.
 * @return 0 — успіх, <0 — код помилки.
 */
int config_reset (void);

/**
 * @brief Перевіряє коректність значень конфігурації.
 * @param c Конфігурація.
 * @param err [out] Буфер повідомлення (може бути NULL).
 * @param errlen Розмір буфера повідомлення.
 * @return 0 — валідно, <0 — код помилки.
 */
int config_validate (const config_t *c, char *err, size_t errlen);

/**
 * @brief Обчислює шлях до файлу конфігурації.
 * @param buf [out] Буфер шляху.
 * @param buflen Довжина буфера.
 * @return 0 — успіх, -1 — помилка/буфер замалий.
 */
int config_get_path (char *buf, size_t buflen);

#endif
