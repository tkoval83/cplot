/**
 * @file cmd.c
 * @brief Реалізації фасаду підкоманд (скорочена назва файлу).
 */
#include "cmd.h"
#include "axidraw.h"
#include "fontreg.h"
#include "help.h"
#include "log.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glob.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/**
 * @brief Сконвертувати config_t у налаштування AxiDraw.
 *
 * @param out Налаштування для заповнення (не NULL).
 * @param cfg Конфігурація CLI (може бути NULL).
 */
static void axidraw_settings_from_config (axidraw_settings_t *out, const config_t *cfg) {
    if (!out)
        return;
    axidraw_settings_reset (out);
    if (!cfg)
        return;
    out->pen_up_delay_ms = cfg->pen_up_delay_ms;
    out->pen_down_delay_ms = cfg->pen_down_delay_ms;
    out->pen_up_pos = cfg->pen_up_pos;
    out->pen_down_pos = cfg->pen_down_pos;
    out->pen_up_speed = cfg->pen_up_speed;
    out->pen_down_speed = cfg->pen_down_speed;
    out->servo_timeout_s = cfg->servo_timeout_s;
    out->speed_mm_s = cfg->speed_mm_s;
    out->accel_mm_s2 = cfg->accel_mm_s2;
}

/**
 * @brief Завантажити та підготувати налаштування для вказаної моделі пристрою.
 *
 * @param model    Ідентифікатор моделі або NULL для типового.
 * @param settings Вихідна структура налаштувань (не NULL).
 * @return true, якщо налаштування отримано; false при помилці.
 */
static bool load_axidraw_settings (const char *model, axidraw_settings_t *settings) {
    if (!settings)
        return false;
    const char *model_id = (model && *model) ? model : CONFIG_DEFAULT_MODEL;
    config_t cfg;
    if (config_factory_defaults (&cfg, model_id) != 0) {
        LOGW ("Не вдалося завантажити заводські налаштування для моделі %s", model_id);
        axidraw_settings_reset (settings);
        return false;
    }
    axidraw_settings_from_config (settings, &cfg);
    return true;
}

/** Кількість мікрокроків на міліметр для типової кінематики AxiDraw. */
#define AXIDRAW_STEPS_PER_MM 80.0

/** Максимальна тривалість команди SM/XM відповідно до протоколу EBB. */
#define AXIDRAW_MAX_DURATION_MS 16777215u

/** Кількість ітерацій очікування стану спокою після керуючої команди. */
#define AXIDRAW_IDLE_WAIT_ATTEMPTS 200

/**
 * @brief Перетворити переміщення у міліметрах у кроки двигунів.
 *
 * @param mm Відстань у міліметрах (може бути від’ємною).
 * @return Кількість мікрокроків, обмежена діапазоном int32_t.
 */
static int32_t mm_to_steps (double mm) {
    double scaled = round (mm * AXIDRAW_STEPS_PER_MM);
    if (scaled > (double)INT32_MAX)
        return INT32_MAX;
    if (scaled < (double)INT32_MIN)
        return INT32_MIN;
    return (int32_t)scaled;
}

/**
 * @brief Обчислити тривалість руху (SM/XM) з урахуванням швидкості.
 *
 * @param distance_mm Довжина траєкторії у міліметрах.
 * @param speed_mm_s  Швидкість у міліметрах за секунду.
 * @return Тривалість у мілісекундах (мінімум 1, максимум AXIDRAW_MAX_DURATION_MS).
 */
static uint32_t motion_duration_ms (double distance_mm, double speed_mm_s) {
    if (distance_mm <= 0.0 || speed_mm_s <= 0.0)
        return 1;
    double ms = ceil ((distance_mm / speed_mm_s) * 1000.0);
    if (ms < 1.0)
        ms = 1.0;
    if (ms > (double)AXIDRAW_MAX_DURATION_MS)
        ms = (double)AXIDRAW_MAX_DURATION_MS;
    return (uint32_t)ms;
}

/**
 * @brief Дочекатися, доки пристрій завершить виконання черги рухів.
 *
 * @param dev Структура пристрою (підключена).
 * @return 0 при успіху; -1, якщо тайм-аут або помилка запиту статусу.
 */
static int wait_for_device_idle (axidraw_device_t *dev) {
    if (!dev)
        return -1;
    struct timespec pause = { .tv_sec = 0, .tv_nsec = 20 * 1000000L };
    for (int attempt = 0; attempt < AXIDRAW_IDLE_WAIT_ATTEMPTS; ++attempt) {
        ebb_status_snapshot_t snapshot;
        if (axidraw_status (dev, &snapshot) != 0)
            return -1;
        if (!snapshot.motion.command_active && !snapshot.motion.motor1_active
            && !snapshot.motion.motor2_active && !snapshot.motion.fifo_pending)
            return 0;
        nanosleep (&pause, NULL);
    }
    LOGW ("AxiDraw не завершив рух протягом очікуваного часу");
    return -1;
}

/**
 * Сигнатура функції, яку виконують дії з підключеним пристроєм.
 */
typedef int (*device_cb_t) (axidraw_device_t *dev, void *ctx);

/**
 * @brief Встановити з’єднання з AxiDraw, виконати дію та безпечно від’єднатися.
 *
 * @param port        Шлях до порту (NULL → авто-пошук).
 * @param model       Ідентифікатор моделі.
 * @param verbose     Рівень деталізації логів.
 * @param action_name Людиночитна назва дії (для логів).
 * @param cb          Функція, яка виконує власне дію (може бути NULL).
 * @param ctx         Додатковий контекст для cb (може бути NULL).
 * @param wait_idle   true → очікувати завершення рухів після виконання cb.
 * @return 0 при успіху; 1 при будь-якій помилці.
 */
static cmd_result_t with_axidraw_device (
    const char *port,
    const char *model,
    verbose_level_t verbose,
    const char *action_name,
    device_cb_t cb,
    void *ctx,
    bool wait_idle) {
    axidraw_settings_t settings;
    if (!load_axidraw_settings (model, &settings)) {
        LOGE ("Не вдалося завантажити налаштування для моделі %s", model ? model : "(типова)");
        return 1;
    }

    axidraw_device_t dev;
    axidraw_device_init (&dev);
    axidraw_apply_settings (&dev, &settings);
    axidraw_device_config (&dev, port, 9600, 5000, settings.min_cmd_interval_ms);

    char errbuf[256];
    if (axidraw_device_connect (&dev, errbuf, sizeof (errbuf)) != 0) {
        LOGE ("Не вдалося підключитися до AxiDraw: %s", errbuf);
        axidraw_device_disconnect (&dev);
        return 1;
    }

    if (verbose == VERBOSE_ON) {
        LOGI ("Виконання дії '%s' на порту %s", action_name, dev.port_path);
    }

    int rc = 0;
    if (cb)
        rc = cb (&dev, ctx);
    if (rc != 0)
        LOGE ("Дія '%s' завершилася з помилкою", action_name);

    int idle_rc = 0;
    if (rc == 0 && wait_idle)
        idle_rc = wait_for_device_idle (&dev);

    axidraw_device_disconnect (&dev);

    if (rc != 0)
        return 1;
    if (wait_idle && idle_rc != 0)
        return 1;
    return 0;
}

/**
 * @brief Підняти перо та повідомити користувача.
 */
static int device_pen_up_cb (axidraw_device_t *dev, void *ctx) {
    (void)ctx;
    int rc = axidraw_pen_up (dev);
    if (rc == 0)
        fprintf (stdout, "Перо піднято\n");
    return rc;
}

/**
 * @brief Опустити перо та повідомити користувача.
 */
static int device_pen_down_cb (axidraw_device_t *dev, void *ctx) {
    (void)ctx;
    int rc = axidraw_pen_down (dev);
    if (rc == 0)
        fprintf (stdout, "Перо опущено\n");
    return rc;
}

/**
 * @brief Перемкнути стан пера, використовуючи поточний статус контролера.
 */
static int device_pen_toggle_cb (axidraw_device_t *dev, void *ctx) {
    (void)ctx;
    bool pen_up = false;
    if (ebb_query_pen (dev->port, &pen_up, dev->timeout_ms) != 0) {
        LOGE ("Не вдалося отримати статус пера");
        return -1;
    }
    int rc = pen_up ? axidraw_pen_down (dev) : axidraw_pen_up (dev);
    if (rc == 0)
        fprintf (stdout, pen_up ? "Перо опущено\n" : "Перо піднято\n");
    return rc;
}

/**
 * @brief Увімкнути мотори зі стандартним мікрокроком.
 */
static int device_motors_on_cb (axidraw_device_t *dev, void *ctx) {
    (void)ctx;
    int rc = ebb_enable_motors (dev->port, EBB_MOTOR_STEP_16, EBB_MOTOR_STEP_16, dev->timeout_ms);
    if (rc == 0)
        fprintf (stdout, "Мотори увімкнено (1/16 мікрокрок)\n");
    return rc;
}

/**
 * @brief Вимкнути мотори для безпечного переміщення вручну.
 */
static int device_motors_off_cb (axidraw_device_t *dev, void *ctx) {
    (void)ctx;
    int rc = ebb_disable_motors (dev->port, dev->timeout_ms);
    if (rc == 0)
        fprintf (stdout, "Мотори вимкнено\n");
    return rc;
}

/**
 * @brief Контекст для ручного зсуву (jog).
 */
struct jog_ctx {
    double dx_mm; /**< Зсув по осі X у мм. */
    double dy_mm; /**< Зсув по осі Y у мм. */
};

/**
 * @brief Виконати короткий зсув у площині XY.
 */
static int device_jog_cb (axidraw_device_t *dev, void *ctx) {
    struct jog_ctx *j = (struct jog_ctx *)ctx;
    double dx = j ? j->dx_mm : 0.0;
    double dy = j ? j->dy_mm : 0.0;
    if (fabs (dx) < 1e-6 && fabs (dy) < 1e-6) {
        LOGW ("Зсув не задано — пропускаємо");
        return 0;
    }

    int rc = ebb_enable_motors (dev->port, EBB_MOTOR_STEP_16, EBB_MOTOR_STEP_16, dev->timeout_ms);
    if (rc != 0)
        return rc;

    int32_t steps_x = mm_to_steps (dx);
    int32_t steps_y = mm_to_steps (dy);
    double distance = hypot (dx, dy);
    const axidraw_settings_t *cfg = axidraw_device_settings (dev);
    double speed = (cfg && cfg->speed_mm_s > 0.0) ? cfg->speed_mm_s : 75.0;
    uint32_t duration = motion_duration_ms (distance, speed);

    rc = axidraw_move_xy (dev, duration, steps_x, steps_y);
    if (rc == 0) {
        if (wait_for_device_idle (dev) == 0) {
            ebb_status_snapshot_t snapshot;
            if (axidraw_status (dev, &snapshot) == 0) {
                double pos_x = snapshot.steps_axis1 / AXIDRAW_STEPS_PER_MM;
                double pos_y = snapshot.steps_axis2 / AXIDRAW_STEPS_PER_MM;
                fprintf (
                    stdout, "Зсув виконано. Поточна позиція: X=%.3f мм, Y=%.3f мм\n", pos_x, pos_y);
            }
        }
    }
    return rc;
}

/**
 * @brief Повернутися у домашню позицію та скинути лічильники кроків.
 */
static int device_home_cb (axidraw_device_t *dev, void *ctx) {
    (void)ctx;
    const axidraw_settings_t *cfg = axidraw_device_settings (dev);
    double speed = (cfg && cfg->speed_mm_s > 0.0) ? cfg->speed_mm_s : 150.0;
    double steps_per_sec = speed * AXIDRAW_STEPS_PER_MM;
    if (steps_per_sec < 100.0)
        steps_per_sec = 100.0;
    if (steps_per_sec > 25000.0)
        steps_per_sec = 25000.0;
    uint32_t step_rate = (uint32_t)steps_per_sec;
    if (step_rate < 2u)
        step_rate = 2u;

    int rc = ebb_enable_motors (dev->port, EBB_MOTOR_STEP_16, EBB_MOTOR_STEP_16, dev->timeout_ms);
    if (rc != 0)
        return rc;

    rc = axidraw_home (dev, step_rate, NULL, NULL);
    if (rc != 0)
        return rc;

    if (wait_for_device_idle (dev) != 0)
        return -1;

    if (ebb_clear_steps (dev->port, dev->timeout_ms) != 0)
        LOGW ("Не вдалося скинути лічильники кроків");

    fprintf (stdout, "Домашнє позиціювання завершено\n");
    return 0;
}

/**
 * @brief Отримати рядок версії прошивки та показати його.
 */
static int device_version_cb (axidraw_device_t *dev, void *ctx) {
    (void)ctx;
    char version[64];
    if (ebb_query_version (dev->port, version, sizeof (version), dev->timeout_ms) != 0) {
        LOGE ("Не вдалося отримати версію контролера");
        return -1;
    }
    fprintf (stdout, "Версія контролера: %s\n", version);
    return 0;
}

/**
 * @brief Показати агрегований статус пристрою.
 */
static int device_status_cb (axidraw_device_t *dev, void *ctx) {
    (void)ctx;
    ebb_status_snapshot_t snapshot;
    if (axidraw_status (dev, &snapshot) != 0)
        return -1;
    fprintf (stdout, "Статус пристрою:\n");
    fprintf (
        stdout, "  Команда активна: %s\n", snapshot.motion.command_active ? "так" : "ні");
    fprintf (
        stdout, "  Мотор X активний: %s\n", snapshot.motion.motor1_active ? "так" : "ні");
    fprintf (
        stdout, "  Мотор Y активний: %s\n", snapshot.motion.motor2_active ? "так" : "ні");
    fprintf (
        stdout, "  FIFO непорожній: %s\n", snapshot.motion.fifo_pending ? "так" : "ні");
    fprintf (stdout, "  Позиція X: %.3f мм\n", snapshot.steps_axis1 / AXIDRAW_STEPS_PER_MM);
    fprintf (stdout, "  Позиція Y: %.3f мм\n", snapshot.steps_axis2 / AXIDRAW_STEPS_PER_MM);
    fprintf (stdout, "  Перо підняте: %s\n", snapshot.pen_up ? "так" : "ні");
    fprintf (stdout, "  Серво увімкнено: %s\n", snapshot.servo_power ? "так" : "ні");
    fprintf (stdout, "  Прошивка: %s\n", snapshot.firmware);
    return 0;
}

/**
 * @brief Показати лише позицію осей у мм.
 */
static int device_position_cb (axidraw_device_t *dev, void *ctx) {
    (void)ctx;
    int32_t steps1 = 0;
    int32_t steps2 = 0;
    if (ebb_query_steps (dev->port, &steps1, &steps2, dev->timeout_ms) != 0) {
        LOGE ("Не вдалося отримати позицію");
        return -1;
    }
    fprintf (stdout, "Поточна позиція: X=%.3f мм, Y=%.3f мм\n", steps1 / AXIDRAW_STEPS_PER_MM,
        steps2 / AXIDRAW_STEPS_PER_MM);
    return 0;
}

/**
 * @brief Скинути лічильники, підняти перо та вимкнути мотори.
 */
static int device_reset_cb (axidraw_device_t *dev, void *ctx) {
    (void)ctx;
    int rc = axidraw_pen_up (dev);
    if (rc != 0)
        return rc;
    if (wait_for_device_idle (dev) != 0)
        return -1;
    if (ebb_clear_steps (dev->port, dev->timeout_ms) != 0)
        LOGW ("Не вдалося скинути лічильники кроків");
    rc = ebb_disable_motors (dev->port, dev->timeout_ms);
    if (rc != 0)
        return rc;
    fprintf (stdout, "Пристрій скинуто: перо підняте, лічильники очищено, мотори вимкнені\n");
    return 0;
}

/**
 * @brief Перезавантажити контролер EBB командою RB.
 */
static int device_reboot_cb (axidraw_device_t *dev, void *ctx) {
    (void)ctx;
    if (serial_write_line (dev->port, "RB") != 0) {
        LOGE ("Не вдалося надіслати команду перезавантаження");
        return -1;
    }
    fprintf (stdout, "Перезавантаження контролера ініційовано\n");
    return 0;
}

/**
 * Виконати підкоманду print (побудова розкладки та відправлення на пристрій).
 *
 * Контракт:
 * - file_path: шлях до вхідного файлу або "-"/NULL для stdin.
 * - Якщо dry_run=true, фактична взаємодія з пристроєм не виконується, лише лог та підготовка.
 * - verbose керує рівнем деталізації журналу (див. verbose_level_t).
 * - На даному етапі реалізація є заглушкою і повертає успіх.
 *
 * @param file_path        Шлях до вхідного файлу або NULL/"-" для stdin.
 * @param font_family      Родина шрифтів або NULL для типової.
 * @param paper_w_mm       Ширина паперу (мм).
 * @param paper_h_mm       Висота паперу (мм).
 * @param margin_top_mm    Верхнє поле (мм).
 * @param margin_right_mm  Праве поле (мм).
 * @param margin_bottom_mm Нижнє поле (мм).
 * @param margin_left_mm   Ліве поле (мм).
 * @param orientation      Значення з enum orientation_t (див. args.h): 1 портрет, 2 альбом.
 * @param dry_run          Імітація без реального виконання.
 * @param verbose          Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_print_execute (
    string_t in,
    const char *font_family,
    double paper_w_mm,
    double paper_h_mm,
    double margin_top_mm,
    double margin_right_mm,
    double margin_bottom_mm,
    double margin_left_mm,
    int orientation,
    bool dry_run,
    verbose_level_t verbose) {
    LOGI ("Почато побудову та друк");
    (void)paper_w_mm;
    (void)paper_h_mm;
    (void)margin_top_mm;
    (void)margin_right_mm;
    (void)margin_bottom_mm;
    (void)margin_left_mm;
    (void)in;
    (void)font_family;
    (void)orientation;
    (void)dry_run;
    (void)verbose;
    fprintf (stdout, "Друк: ще не реалізовано\n");
    return 0;
}

/**
 * Обгортка для попереднього перегляду: виклик cmd_print_execute у режимі preview.
 */
cmd_result_t cmd_print_preview_execute (
    string_t in,
    const char *font_family,
    double paper_w_mm,
    double paper_h_mm,
    double margin_top_mm,
    double margin_right_mm,
    double margin_bottom_mm,
    double margin_left_mm,
    int orientation,
    preview_fmt_t format,
    verbose_level_t verbose,
    bytes_t *out) {
    (void)in;
    (void)font_family;
    (void)paper_w_mm;
    (void)paper_h_mm;
    (void)margin_top_mm;
    (void)margin_right_mm;
    (void)margin_bottom_mm;
    (void)margin_left_mm;
    (void)orientation;
    (void)verbose;
    LOGI ("Попередній перегляд");
    if (!out) {
        LOGE ("Внутрішня помилка: не задано вихідні параметри прев’ю");
        return 1;
    }
    out->bytes = NULL;
    out->len = 0;

    /* Тимчасова заглушка: повертаємо мінімальний валідний SVG або PNG-заглушку. */
    if (format == PREVIEW_FMT_SVG) {
        static const char svg_stub[]
            = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"50\">"
              "<text x=\"10\" y=\"25\" font-family=\"sans-serif\" font-size=\"12\">"
              "прев’ю ще не реалізовано"
              "</text></svg>";
        size_t n = sizeof (svg_stub) - 1;
        uint8_t *buf = (uint8_t *)malloc (n);
        if (!buf) {
            LOGE ("Недостатньо пам'яті для формування прев’ю");
            return 1;
        }
        memcpy (buf, svg_stub, n);
        out->bytes = buf;
        out->len = n;
        return 0;
    } else {
        /* PNG-заглушка: найменший валідний PNG (однопіксельний) у вигляді константного масиву */
        static const uint8_t png_stub[]
            = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48,
                0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00,
                0x00, 0x90, 0x77, 0x53, 0xDE, 0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41, 0x54, 0x78,
                0x9C, 0x63, 0x60, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0xE5, 0x27, 0xD4, 0xA2, 0x00,
                0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };
        size_t n = sizeof (png_stub);
        uint8_t *buf = (uint8_t *)malloc (n);
        if (!buf) {
            LOGE ("Недостатньо пам'яті для формування прев’ю");
            return 1;
        }
        memcpy (buf, png_stub, n);
        out->bytes = buf;
        out->len = n;
        return 0;
    }
}

/**
 * Згенерувати прев’ю розкладки у stdout (SVG або PNG).
 *
 * Контракт:
 * - Не здійснює взаємодії з пристроєм; лише побудова розкладки та вивід у stdout.
 * - Формат виводу задається параметром format.
 * - На даному етапі реалізація є заглушкою і повертає успіх.
 *
 * @param file_path        Шлях до вхідного файлу або NULL/"-" для stdin.
 * @param font_family      Родина шрифтів або NULL.
 * @param paper_w_mm       Ширина паперу (мм).
 * @param paper_h_mm       Висота паперу (мм).
 * @param margin_top_mm    Верхнє поле (мм).
 * @param margin_right_mm  Праве поле (мм).
 * @param margin_bottom_mm Нижнє поле (мм).
 * @param margin_left_mm   Ліве поле (мм).
 * @param orientation      Значення з enum orientation_t (див. args.h): 1 портрет, 2 альбом.
 * @param dry_run          Імітація без реального виконання (для симетрії з print).
 * @param format           Формат прев’ю (SVG або PNG).
 * @param verbose          Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
/* видалено окрему реалізацію прев'ю; інтегровано у cmd_print_execute */

/**
 * Показати версію програми.
 *
 * @param verbose Рівень деталізації логів.
 * @return 0 успіх.
 */
cmd_result_t cmd_version_execute (verbose_level_t verbose) {
    if (verbose)
        LOGI ("Докладний режим виводу");
    version ();
    return 0;
}

/**
 * Вивести перелік доступних векторних шрифтів.
 *
 * @param verbose Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_fonts_execute (verbose_level_t verbose) { return cmd_font_list_execute (verbose); }

/**
 * Операції над конфігурацією (show/reset/set).
 *
 * @param action     Дія (див. config_action_t у args.h).
 * @param set_pairs  Пара(и) key=value через кому (для action==SET) або NULL.
 * @param inout_cfg  Конфігурація для читання/зміни.
 * @param verbose    Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_config_execute (
    int action, const char *set_pairs, config_t *inout_cfg, verbose_level_t verbose) {
    switch (action) {
    case 0:
        LOGW ("Не вказано дію для налаштувань");
        return 2;
    case 1: /* CFG_SHOW */
        return cmd_config_show (inout_cfg, verbose);
    case 2: /* CFG_RESET */
        return cmd_config_reset (inout_cfg, verbose);
    case 3: /* CFG_SET */
        return cmd_config_set (set_pairs ? set_pairs : "", inout_cfg, verbose);
    default:
        LOGW ("Невідома дія для налаштувань");
        return 2;
    }
}

/**
 * Операції над пристроєм (list/jog/pen/...)
 *
 * @param action   Дія пристрою (див. device_action_t у args.h).
 * @param port     Шлях до серійного порту або NULL для автопошуку.
 * @param model    Ідентифікатор моделі або NULL для типової.
 * @param jog_dx_mm Зсув по X у мм для дії jog.
 * @param jog_dy_mm Зсув по Y у мм для дії jog.
 * @param verbose  Рівень деталізації логів.
 * @return 0 успіх; ненульовий код — помилка.
 */
cmd_result_t cmd_device_execute (
    const device_action_t *action,
    const char *port,
    const char *model,
    double jog_dx_mm,
    double jog_dy_mm,
    verbose_level_t verbose) {
    if (!action || action->kind == DEVICE_ACTION_NONE) {
        LOGW ("Не вказано дію для пристрою");
        return 2;
    }

    switch (action->kind) {
    case DEVICE_ACTION_LIST:
        return cmd_device_list (model, verbose);
    case DEVICE_ACTION_PEN:
        switch (action->pen) {
        case DEVICE_PEN_UP:
            return cmd_device_pen_up (port, model, verbose);
        case DEVICE_PEN_DOWN:
            return cmd_device_pen_down (port, model, verbose);
        case DEVICE_PEN_TOGGLE:
            return cmd_device_pen_toggle (port, model, verbose);
        default:
            LOGW ("Невідома дія пера");
            return 2;
        }
    case DEVICE_ACTION_MOTORS:
        switch (action->motor) {
        case DEVICE_MOTOR_ON:
            return cmd_device_motors_on (port, model, verbose);
        case DEVICE_MOTOR_OFF:
            return cmd_device_motors_off (port, model, verbose);
        default:
            LOGW ("Невідома дія моторів");
            return 2;
        }
    case DEVICE_ACTION_HOME:
        return cmd_device_home (port, model, verbose);
    case DEVICE_ACTION_JOG:
        return cmd_device_jog (port, model, jog_dx_mm, jog_dy_mm, verbose);
    case DEVICE_ACTION_VERSION:
        return cmd_device_version (port, model, verbose);
    case DEVICE_ACTION_STATUS:
        return cmd_device_status (port, model, verbose);
    case DEVICE_ACTION_POSITION:
        return cmd_device_position (port, model, verbose);
    case DEVICE_ACTION_RESET:
        return cmd_device_reset (port, model, verbose);
    case DEVICE_ACTION_REBOOT:
        return cmd_device_reboot (port, model, verbose);
    default:
        LOGW ("Невідома категорія дій пристрою");
        return 2;
    }
}

/**
 * Вивести діагностичну інформацію системи.
 *
 * @param verbose Рівень деталізації логів.
 * @return 0 успіх.
 */
cmd_result_t cmd_sysinfo_execute (verbose_level_t verbose) {
    (void)verbose;
    LOGI ("Системна інформація");
    fprintf (stdout, "Системна інформація: ще не реалізовано\n");
    return 0;
}

/* ---- Реалізації деталізованих функцій (заглушки з логами) ---- */

/// Вивести перелік доступних векторних шрифтів (деталізована функція).
cmd_result_t cmd_font_list_execute (verbose_level_t verbose) {
    (void)verbose;
    LOGI ("Перелік доступних шрифтів");
    font_face_t *faces = NULL;
    size_t count = 0;
    int rc = fontreg_list (&faces, &count);
    if (rc != 0) {
        fprintf (stdout, "Не вдалося завантажити реєстр шрифтів (код %d)\n", rc);
        return 1;
    }

    fprintf (stdout, "Доступні шрифти (%zu):\n", count);
    for (size_t i = 0; i < count; ++i) {
        fprintf (stdout, "  - %-24s — %s\n", faces[i].id, faces[i].name);
    }
    free (faces);
    return 0;
}

/// Показати поточну конфігурацію (config --show).
cmd_result_t cmd_config_show (const config_t *cfg, verbose_level_t verbose) {
    (void)cfg;
    (void)verbose;
    LOGI ("Поточні налаштування");
    fprintf (stdout, "Налаштування (показ): ще не реалізовано\n");
    return 0;
}

/// Скинути конфігурацію до типових значень (config --reset).
cmd_result_t cmd_config_reset (config_t *inout_cfg, verbose_level_t verbose) {
    (void)inout_cfg;
    (void)verbose;
    LOGI ("Скидання налаштувань");
    fprintf (stdout, "Налаштування (скидання): ще не реалізовано\n");
    return 0;
}

/// Встановити значення конфігурації за парами key=value (config --set).
cmd_result_t cmd_config_set (const char *set_pairs, config_t *inout_cfg, verbose_level_t verbose) {
    (void)inout_cfg;
    (void)verbose;
    (void)set_pairs;
    LOGI ("Застосування нових налаштувань");
    fprintf (stdout, "Налаштування (встановлення): ще не реалізовано\n");
    return 0;
}

/**
 * @brief Опис потенційного серійного порту AxiDraw для device --list.
 */
typedef struct {
    char path[PATH_MAX];    /**< Повний шлях до порту. */
    bool responsive;        /**< true, якщо контролер відповів на запит V. */
    char version[64];       /**< Рядок версії прошивки, якщо доступний. */
    char detail[128];       /**< Пояснення у разі помилки. */
} device_port_info_t;

/**
 * @brief Перевірити, чи вже присутній шлях у масиві портів.
 */
static bool device_port_exists (const device_port_info_t *ports, size_t count, const char *path) {
    if (!ports || !path)
        return false;
    for (size_t i = 0; i < count; ++i) {
        if (strcmp (ports[i].path, path) == 0)
            return true;
    }
    return false;
}

/**
 * @brief Додати новий порт до динамічного масиву, забезпечивши унікальність.
 *
 * @param ports    Вказівник на масив (realloc-ується всередині).
 * @param count    Поточна кількість елементів.
 * @param capacity Поточна місткість масиву.
 * @param path     Шлях, який потрібно додати.
 * @return true при успіху; false у разі помилки алокації.
 */
static bool device_port_add (
    device_port_info_t **ports,
    size_t *count,
    size_t *capacity,
    const char *path) {
    if (!ports || !count || !capacity || !path || !*path)
        return false;
    if (*ports && device_port_exists (*ports, *count, path))
        return true;
    if (*count == *capacity) {
        size_t new_cap = (*capacity == 0) ? 4 : (*capacity * 2);
        device_port_info_t *np
            = (device_port_info_t *)realloc (*ports, new_cap * sizeof (device_port_info_t));
        if (!np)
            return false;
        *ports = np;
        *capacity = new_cap;
    }
    device_port_info_t *slot = &(*ports)[*count];
    memset (slot, 0, sizeof (*slot));
    strncpy (slot->path, path, sizeof (slot->path) - 1);
    slot->path[sizeof (slot->path) - 1] = '\0';
    ++(*count);
    return true;
}

/// Перелічити доступні порти пристроїв (device --list).
cmd_result_t cmd_device_list (const char *model, verbose_level_t verbose) {
    (void)model;
    (void)verbose;
    LOGI ("Перелік доступних портів");

    static const char *patterns[]
        = { "/dev/serial/by-id/usb-EiBotBoard*", "/dev/serial/by-id/usb-*-EiBotBoard*",
              "/dev/serial/by-id/usb-*04d8*FD92*", "/dev/serial/by-id/usb-*04D8*FD92*",
              "/dev/tty.usbserial-EiBotBoard*", "/dev/cu.usbserial-EiBotBoard*",
              "/dev/cu.usbmodem*", "/dev/cu.usbserial*", "/dev/tty.usbmodem*",
              "/dev/tty.usbserial*", "/dev/ttyACM*", "/dev/ttyUSB*" };

    device_port_info_t *ports = NULL;
    size_t count = 0;
    size_t capacity = 0;

    for (size_t i = 0; i < sizeof (patterns) / sizeof (patterns[0]); ++i) {
        glob_t g;
        memset (&g, 0, sizeof (g));
        int grc = glob (patterns[i], 0, NULL, &g);
        if (grc == 0) {
            for (size_t j = 0; j < g.gl_pathc; ++j) {
                if (!device_port_add (&ports, &count, &capacity, g.gl_pathv[j])) {
                    LOGE ("Недостатньо пам'яті для переліку портів");
                    globfree (&g);
                    free (ports);
                    return 1;
                }
            }
        } else if (grc != GLOB_NOMATCH) {
            LOGW ("Не вдалося опрацювати шаблон портів %s (код %d)", patterns[i], grc);
        }
        globfree (&g);
    }

#ifdef __APPLE__
    char guessed[PATH_MAX];
    if (serial_guess_axidraw_port (guessed, sizeof (guessed)) == 0) {
        if (!device_port_add (&ports, &count, &capacity, guessed)) {
            LOGE ("Недостатньо пам'яті для переліку портів");
            free (ports);
            return 1;
        }
    }
#endif

    if (!ports) {
        fprintf (stdout, "Не вдалося перерахувати порти (нестача пам’яті).\n");
        return 1;
    }

    if (count == 0) {
        fprintf (stdout, "Потенційних портів AxiDraw не знайдено. Підключіть пристрій і повторіть.\n");
        free (ports);
        return 0;
    }

    for (size_t i = 0; i < count; ++i) {
        char errbuf[128] = { 0 };
        serial_port_t *sp = serial_open (ports[i].path, 9600, 2000, errbuf, sizeof (errbuf));
        if (!sp) {
            if (errbuf[0])
                strncpy (ports[i].detail, errbuf, sizeof (ports[i].detail) - 1);
            else
                strncpy (ports[i].detail, "не вдалося відкрити порт", sizeof (ports[i].detail) - 1);
            ports[i].detail[sizeof (ports[i].detail) - 1] = '\0';
            continue;
        }
        char version[sizeof (ports[i].version)];
        if (serial_probe_ebb (sp, version, sizeof (version)) == 0) {
            ports[i].responsive = true;
            strncpy (ports[i].version, version, sizeof (ports[i].version) - 1);
            ports[i].version[sizeof (ports[i].version) - 1] = '\0';
        } else {
            strncpy (ports[i].detail, "немає відповіді від контролера", sizeof (ports[i].detail) - 1);
            ports[i].detail[sizeof (ports[i].detail) - 1] = '\0';
        }
        serial_close (sp);
    }

    fprintf (stdout, "Знайдені порти (%zu):\n", count);
    for (size_t i = 0; i < count; ++i) {
        if (ports[i].responsive) {
            fprintf (
                stdout, "  - %s — контролер відповідає (версія %s)\n", ports[i].path,
                ports[i].version[0] ? ports[i].version : "невідома");
        } else if (ports[i].detail[0]) {
            fprintf (stdout, "  - %s — %s\n", ports[i].path, ports[i].detail);
        } else {
            fprintf (stdout, "  - %s — без відповіді від AxiDraw\n", ports[i].path);
        }
    }

    free (ports);
    return 0;
}

/// Підняти перо (device up).
cmd_result_t cmd_device_pen_up (const char *port, const char *model, verbose_level_t verbose) {
    LOGI ("Підйом пера");
    return with_axidraw_device (port, model, verbose, "підйом пера", device_pen_up_cb, NULL, true);
}

/// Опустити перо (device down).
cmd_result_t cmd_device_pen_down (const char *port, const char *model, verbose_level_t verbose) {
    LOGI ("Опускання пера");
    return with_axidraw_device (port, model, verbose, "опускання пера", device_pen_down_cb, NULL,
        true);
}

/// Перемкнути стан пера (device toggle).
cmd_result_t cmd_device_pen_toggle (const char *port, const char *model, verbose_level_t verbose) {
    LOGI ("Перемикання пера");
    return with_axidraw_device (
        port, model, verbose, "перемикання пера", device_pen_toggle_cb, NULL, true);
}

/// Увімкнути мотори (device motors-on).
cmd_result_t cmd_device_motors_on (const char *port, const char *model, verbose_level_t verbose) {
    LOGI ("Увімкнення моторів");
    return with_axidraw_device (
        port, model, verbose, "увімкнення моторів", device_motors_on_cb, NULL, false);
}

/// Вимкнути мотори (device motors-off).
cmd_result_t cmd_device_motors_off (const char *port, const char *model, verbose_level_t verbose) {
    LOGI ("Вимкнення моторів");
    return with_axidraw_device (
        port, model, verbose, "вимкнення моторів", device_motors_off_cb, NULL, false);
}

/// Перемістити у початкову позицію (home).
cmd_result_t cmd_device_home (const char *port, const char *model, verbose_level_t verbose) {
    LOGI ("Повернення у початкову позицію");
    return with_axidraw_device (port, model, verbose, "повернення у домашню позицію",
        device_home_cb, NULL, false);
}

/// Ручний зсув на dx/dy у мм (jog).
cmd_result_t cmd_device_jog (
    const char *port, const char *model, double dx_mm, double dy_mm, verbose_level_t verbose) {
    LOGI ("Ручний зсув: по іксу %.3f мм, по ігреку %.3f мм", dx_mm, dy_mm);
    struct jog_ctx ctx = { .dx_mm = dx_mm, .dy_mm = dy_mm };
    return with_axidraw_device (
        port, model, verbose, "ручний зсув", device_jog_cb, &ctx, false);
}

/// Показати версію контролера (device version).
cmd_result_t cmd_device_version (const char *port, const char *model, verbose_level_t verbose) {
    LOGI ("Версія контролера");
    return with_axidraw_device (
        port, model, verbose, "версія контролера", device_version_cb, NULL, false);
}

/// Показати статус пристрою (device status).
cmd_result_t cmd_device_status (const char *port, const char *model, verbose_level_t verbose) {
    LOGI ("Стан пристрою");
    return with_axidraw_device (
        port, model, verbose, "статус пристрою", device_status_cb, NULL, false);
}

/// Показати поточну позицію (device position).
cmd_result_t cmd_device_position (const char *port, const char *model, verbose_level_t verbose) {
    LOGI ("Поточна позиція");
    return with_axidraw_device (
        port, model, verbose, "поточна позиція", device_position_cb, NULL, false);
}

/// Скинути контролер (device reset).
cmd_result_t cmd_device_reset (const char *port, const char *model, verbose_level_t verbose) {
    LOGI ("Скидання контролера");
    return with_axidraw_device (
        port, model, verbose, "скидання стану", device_reset_cb, NULL, false);
}

/// Перезавантажити контролер (device reboot).
cmd_result_t cmd_device_reboot (const char *port, const char *model, verbose_level_t verbose) {
    LOGI ("Перезавантаження контролера");
    return with_axidraw_device (
        port, model, verbose, "перезавантаження контролера", device_reboot_cb, NULL, false);
}
