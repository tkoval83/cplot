#define _GNU_SOURCE

/**
 * @file cmd.c
 * @brief Реалізації фасаду підкоманд (скорочена назва файлу).
 */
#include "cmd.h"
#include "axidraw.h"
#include "config.h"
#include "drawing.h"
#include "fontreg.h"
#include "geom.h"
#include "log.h"
#include "markdown.h"
#include "png.h"
#include "proginfo.h"
#include "svg.h"

#include "serial.h"
#include "str.h"
#include <ctype.h>
#include <glob.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

static __thread FILE *g_cmd_out = NULL;
/* Локальний тип формату превʼю (не експортується у заголовок) */
typedef enum {
    PREVIEW_FMT_SVG = 0,
    PREVIEW_FMT_PNG = 1,
} preview_fmt_t;
static int layout_to_bytes (
    const drawing_layout_t *layout, preview_fmt_t fmt, uint8_t **out_bytes, size_t *out_len);

/* Локальні типи */
typedef enum { VERBOSE_OFF = 0, VERBOSE_ON = 1 } verbose_level_t;

/* Вперед-оголошення */

/**
 * @brief Отримати поточний вихідний потік для команд.
 *
 * Повертає встановлений через `cmd_set_output()` потік або `stdout` за замовчуванням.
 */
static FILE *cmd_output_stream (void) { return g_cmd_out ? g_cmd_out : stdout; }

/**
 * @brief Встановити альтернативний потік виводу для командних повідомлень.
 *
 * Функція потокобезпечна завдяки використанню TLS (`__thread`). Передайте NULL, щоб
 * повернутись до `stdout`.
 *
 * @param out Потік виводу або NULL.
 */
void cmd_set_output (FILE *out) { g_cmd_out = out; }

#define CMD_OUT cmd_output_stream ()

/**
 * @brief Побудувати структуру налаштувань AxiDraw на основі конфігурації.
 *
 * @param[out] out Місце для запису налаштувань (не NULL).
 * @param cfg      Джерело конфігурації; якщо NULL — використовується стан за замовчуванням.
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
 * @brief Завантажити налаштування AxiDraw для конкретної моделі.
 *
 * @param model     Ідентифікатор моделі (може бути NULL).
 * @param[out] settings Структура для результату (не NULL).
 * @return `true`, якщо налаштування успішно зібрані; `false` у разі помилки.
 */
static bool load_axidraw_settings (const char *model, axidraw_settings_t *settings) {
    if (!settings)
        return false;
    const char *model_id = (model && *model) ? model : CONFIG_DEFAULT_MODEL;
    config_t cfg;
    if (config_factory_defaults (&cfg, model_id) != 0) {
        LOGW ("Не вдалося ініціалізувати базову конфігурацію для моделі %s", model_id);
        axidraw_settings_reset (settings);
        return false;
    }
    const axidraw_device_profile_t *profile = axidraw_device_profile_for_model (model_id);
    axidraw_device_profile_apply (&cfg, profile);
    axidraw_settings_from_config (settings, &cfg);
    /* Пропагуємо steps_per_mm із профілю */
    if (profile && profile->steps_per_mm > 0.0) {
        settings->steps_per_mm = profile->steps_per_mm;
    } else if (!(settings->steps_per_mm > 0.0)) {
        const axidraw_device_profile_t *defp = axidraw_device_profile_default ();
        if (defp && defp->steps_per_mm > 0.0)
            settings->steps_per_mm = defp->steps_per_mm;
    }
    /* Жорстка перевірка апаратної характеристики */
    if (!(settings->steps_per_mm > 0.0)) {
        LOGE (
            "Профіль моделі '%s' має некоректний коефіцієнт кроків на міліметр — перервано",
            model_id);
        return false;
    }
    if (profile && strcasecmp (model_id, profile->model) != 0)
        LOGI ("Невідомий профіль %s — використано %s", model_id, profile->model);
    return true;
}

#define AXIDRAW_MAX_DURATION_MS 16777215u
#define AXIDRAW_IDLE_WAIT_ATTEMPTS 200

/**
 * @brief Попередити користувача, що пристрій зайнятий іншим процесом.
 */
static void warn_device_busy (void) {
    char holder[64] = "невідомий процес";
    const char *lock_path = axidraw_device_lock_file ();
    FILE *lf = fopen (lock_path, "r");
    if (lf) {
        if (fgets (holder, sizeof (holder), lf)) {
            holder[strcspn (holder, "\r\n")] = '\0';
            if (holder[0] == '\0')
                snprintf (holder, sizeof (holder), "%s", "невідомий процес");
        }
        fclose (lf);
    }
    LOGW ("Пристрій вже використовується (%s)", holder);
}

#ifdef DEBUG
static void
debug_log_snapshot (const char *phase, bool have_snapshot, const ebb_status_snapshot_t *snap) {
    if (!have_snapshot || !snap) {
        LOGD ("налагодження[%s]: статус недоступний", phase);
        return;
    }
    LOGD (
        "налагодження[%s]: активна=%d моторX=%d моторY=%d fifo=%d перо_піднято=%d "
        "позиція=(%ld,%ld)",
        phase, snap->motion.command_active, snap->motion.motor1_active, snap->motion.motor2_active,
        snap->motion.fifo_pending, snap->pen_up, (long)snap->steps_axis1, (long)snap->steps_axis2);
}
#else
static void
debug_log_snapshot (const char *phase, bool have_snapshot, const ebb_status_snapshot_t *snap) {
    (void)phase;
    (void)have_snapshot;
    (void)snap;
}
#endif

/**
 * @brief Зчитати та залогувати стан пристрою після виконання команди.
 *
 * @param dev        Пристрій AxiDraw.
 * @param phase      Позначка етапу (before/after тощо).
 * @param action     Людиночиткий опис дії.
 * @param command_rc Код повернення основної дії.
 * @param wait_rc    Результат очікування простою (якщо застосовно).
 */
static void update_state_from_device (
    axidraw_device_t *dev, const char *phase, const char *action, int command_rc, int wait_rc) {
    (void)command_rc;
    (void)wait_rc;
    (void)action;
    ebb_status_snapshot_t snap;
    bool ok = false;
    if (dev && axidraw_device_is_connected (dev))
        ok = axidraw_status (dev, &snap) == 0;
    debug_log_snapshot (phase, ok, ok ? &snap : NULL);
}

/**
 * @brief Конвертувати міліметри у кроки AxiDraw із захистом від переповнення.
 *
 * @param mm Відстань у міліметрах.
 * @return Кількість кроків у межах int32.
 */
static int32_t mm_to_steps (axidraw_device_t *dev, double mm) {
    const axidraw_settings_t *cfg = dev ? axidraw_device_settings (dev) : NULL;
    double steps_per_mm = (cfg && cfg->steps_per_mm > 0.0) ? cfg->steps_per_mm : 0.0;

    if (!(steps_per_mm > 0.0)) {
        LOGE ("Коефіцієнт кроків на міліметр не ініціалізовано — профіль пристрою не застосовано");
        return 0;
    }

    if (!(mm > -1e300 && mm < 1e300)) { /* захист від NaN/Inf */
        LOGW ("Некоректне значення відстані (mm=%.2f) — повертаю 0", mm);
        return 0;
    }

    double scaled = round (mm * steps_per_mm);

    if (scaled > (double)INT32_MAX) {
        LOGW ("Конвертація міліметрів у кроки призвела до переповнення — обмежено максимумом");
        return INT32_MAX;
    }
    if (scaled < (double)INT32_MIN) {
        LOGW ("Конвертація міліметрів у кроки призвела до від’ємного переповнення — обмежено "
              "мінімумом");
        return INT32_MIN;
    }

    /* Дрібне попередження, якщо абсолютне значення незвично велике (може вказувати на помилку
     * одиниць, наприклад подали мікрони замість мм). */
    if (fabs (scaled) > 5e7) {
        LOGW ("Нетипово велика кількість кроків для заданої відстані — перевірте одиниці");
    }

    return (int32_t)scaled;
}

/**
 * @brief Обчислити тривалість руху в мс для заданої відстані та швидкості.
 *
 * @param distance_mm Відстань у міліметрах.
 * @param speed_mm_s  Швидкість у мм/с.
 * @return Тривалість у мс (мінімум 1, максимум `AXIDRAW_MAX_DURATION_MS`).
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

/* ===================== SHAPE (підкоманда) ===================== */

/* Вперед-оголошення для локальної утиліти build_print_page(), оголошеної нижче. */
static int build_print_page (
    drawing_page_t *page,
    const char *device_model,
    double paper_w_mm,
    double paper_h_mm,
    double margin_top_mm,
    double margin_right_mm,
    double margin_bottom_mm,
    double margin_left_mm,
    int orientation);

/* Функціонал фігур у цьому модулі не використовується. */

/**
 * @brief Дочекатися, поки пристрій завершить рух та очистить FIFO.
 *
 * @param dev Пристрій AxiDraw.
 * @return 0, якщо пристрій зупинився вчасно; -1 при тайм-ауті або помилці.
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
    LOGW ("Пристрій не завершив рух протягом очікуваного часу");
    return -1;
}

typedef int (*device_cb_t) (axidraw_device_t *dev, void *ctx);

/**
 * @brief Універсальний шаблон виконання дії над пристроєм AxiDraw.
 *
 * Піклується про блокування, підключення, застосування налаштувань і очікування завершення
 * команди. Колбек виконується лише після успішного підключення.
 *
 * @param port        Шлях до порту (може бути NULL — тоді буде знайдено автоматично).
 * @param model       Модель пристрою для завантаження конфігурації (може бути NULL).
 * @param verbose     Режим виводу (on/off).
 * @param action_name Людиночиткий опис дії для логів.
 * @param cb          Колбек, який виконує безпосередню операцію.
 * @param ctx         Контекст, що передається у колбек (може бути NULL).
 * @param wait_idle   Чи потрібно чекати завершення руху перед відʼєднанням.
 * @return 0 при успіху; 1 у разі помилки або якщо пристрій зайнятий.
 */
static int with_axidraw_device (
    const char *port,
    const char *model,
    verbose_level_t verbose,
    const char *action_name,
    device_cb_t cb,
    void *ctx,
    bool wait_idle) {
    int status = 0;
    int lock_fd = -1;
    if (axidraw_device_lock_acquire (&lock_fd) != 0) {
        warn_device_busy ();
        return 1;
    }

    axidraw_settings_t settings;
    if (!load_axidraw_settings (model, &settings)) {
        LOGE ("Не вдалося завантажити налаштування для моделі %s", model ? model : "(типова)");
        status = 1;
        goto cleanup;
    }

    axidraw_device_t dev;
    axidraw_device_init (&dev);
    axidraw_apply_settings (&dev, &settings);
    axidraw_device_config (&dev, port, 9600, 5000, settings.min_cmd_interval_ms);

    char errbuf[256];
    if (axidraw_device_connect (&dev, errbuf, sizeof (errbuf)) != 0) {
        LOGE ("Не вдалося підключитися до пристрою: %s", errbuf);
        axidraw_device_disconnect (&dev);
        status = 1;
        goto cleanup;
    }

    if (verbose == VERBOSE_ON)
        LOGI ("Виконання дії '%s' на порту %s", action_name, dev.port_path);

    update_state_from_device (&dev, "before", action_name, 0, 0);

    int rc = 0;
    if (cb)
        rc = cb (&dev, ctx);
    if (rc != 0)
        LOGE ("Дія '%s' завершилася з помилкою", action_name);

    int idle_rc = 0;
    if (rc == 0 && wait_idle)
        idle_rc = wait_for_device_idle (&dev);

    const char *phase
        = (rc != 0) ? "error" : ((wait_idle && idle_rc == 0) ? "after_wait" : "after");
    update_state_from_device (&dev, phase, action_name, rc, wait_idle ? idle_rc : 0);

    axidraw_device_disconnect (&dev);

    if (rc != 0)
        status = 1;
    else if (wait_idle && idle_rc != 0)
        status = 1;

cleanup:
    axidraw_device_lock_release (lock_fd);
    return status;
}

/**
 * @brief Колбек для підняття пера.
 */
static int device_pen_up_cb (axidraw_device_t *dev, void *ctx) {
    (void)ctx;
    int rc = axidraw_pen_up (dev);
    if (rc == 0)
        fprintf (CMD_OUT, "Перо піднято\n");
    return rc;
}

/**
 * @brief Колбек для опускання пера.
 */
static int device_pen_down_cb (axidraw_device_t *dev, void *ctx) {
    (void)ctx;
    int rc = axidraw_pen_down (dev);
    if (rc == 0)
        fprintf (CMD_OUT, "Перо опущено\n");
    return rc;
}

/**
 * @brief Колбек для перемикання стану пера.
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
        fprintf (CMD_OUT, pen_up ? "Перо опущено\n" : "Перо піднято\n");
    return rc;
}

/**
 * @brief Колбек для ввімкнення моторів.
 */
static int device_motors_on_cb (axidraw_device_t *dev, void *ctx) {
    (void)ctx;
    int rc = ebb_enable_motors (dev->port, EBB_MOTOR_STEP_16, EBB_MOTOR_STEP_16, dev->timeout_ms);
    if (rc == 0)
        fprintf (CMD_OUT, "Мотори увімкнено (1/16 мікрокрок)\n");
    return rc;
}

/**
 * @brief Колбек для вимкнення моторів.
 */
static int device_motors_off_cb (axidraw_device_t *dev, void *ctx) {
    (void)ctx;
    int rc = ebb_disable_motors (dev->port, dev->timeout_ms);
    if (rc == 0)
        fprintf (CMD_OUT, "Мотори вимкнено\n");
    return rc;
}

/**
 * @brief Колбек для аварійної зупинки.
 */
static int device_abort_cb (axidraw_device_t *dev, void *ctx) {
    (void)ctx;
    int rc = axidraw_emergency_stop (dev);
    if (rc == 0)
        fprintf (CMD_OUT, "Аварійна зупинка виконана\n");
    return rc;
}

typedef struct {
    double dx_mm;
    double dy_mm;
} jog_ctx_t;

/**
 * @brief Колбек для ручного зсуву (`jog`).
 */
static int device_jog_cb (axidraw_device_t *dev, void *ctx) {
    jog_ctx_t *jog = (jog_ctx_t *)ctx;
    double dx = jog ? jog->dx_mm : 0.0;
    double dy = jog ? jog->dy_mm : 0.0;
    if (fabs (dx) < 1e-6 && fabs (dy) < 1e-6) {
        LOGW ("Зсув не задано — пропускаємо");
        return 0;
    }

    int rc = ebb_enable_motors (dev->port, EBB_MOTOR_STEP_16, EBB_MOTOR_STEP_16, dev->timeout_ms);
    if (rc != 0)
        return rc;

    int32_t steps_x = mm_to_steps (dev, dx);
    int32_t steps_y = mm_to_steps (dev, dy);
    double distance = hypot (dx, dy);
    const axidraw_settings_t *cfg = axidraw_device_settings (dev);
    double speed = (cfg && cfg->speed_mm_s > 0.0) ? cfg->speed_mm_s : 75.0;
    uint32_t duration = motion_duration_ms (distance, speed);

    rc = axidraw_move_xy (dev, duration, steps_x, steps_y);
    if (rc == 0 && wait_for_device_idle (dev) == 0) {
        ebb_status_snapshot_t snapshot;
        if (axidraw_status (dev, &snapshot) == 0) {
            double steps_per_mm = (cfg && cfg->steps_per_mm > 0.0) ? cfg->steps_per_mm : 0.0;
            if (!(steps_per_mm > 0.0)) {
                LOGE ("Коефіцієнт кроків на міліметр не встановлено (профіль відсутній)");
            } else {
                double pos_x = snapshot.steps_axis1 / steps_per_mm;
                double pos_y = snapshot.steps_axis2 / steps_per_mm;
                fprintf (
                    CMD_OUT, "Зсув виконано. Поточна позиція: X=%.3f мм, Y=%.3f мм\n", pos_x,
                    pos_y);
            }
        }
    }
    return rc;
}

/**
 * @brief Колбек для повернення у домашню позицію.
 */
static int device_home_cb (axidraw_device_t *dev, void *ctx) {
    (void)ctx;
    const axidraw_settings_t *cfg = axidraw_device_settings (dev);
    double speed = (cfg && cfg->speed_mm_s > 0.0) ? cfg->speed_mm_s : 150.0;
    double steps_per_mm = (cfg && cfg->steps_per_mm > 0.0) ? cfg->steps_per_mm : 0.0;
    if (!(steps_per_mm > 0.0)) {
        LOGE ("Коефіцієнт кроків на міліметр не встановлено — перериваємо повернення у базову "
              "позицію");
        return -1;
    }
    double steps_per_sec = speed * steps_per_mm;
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

    fprintf (CMD_OUT, "Домашнє позиціювання завершено\n");
    return 0;
}

/**
 * @brief Колбек для запиту версії прошивки.
 */
static int device_version_cb (axidraw_device_t *dev, void *ctx) {
    (void)ctx;
    char version[64];
    if (ebb_query_version (dev->port, version, sizeof (version), dev->timeout_ms) != 0) {
        LOGE ("Не вдалося отримати версію контролера");
        return -1;
    }
    fprintf (CMD_OUT, "Версія контролера: %s\n", version);
    return 0;
}

/**
 * @brief Колбек для друку поточного статусу пристрою.
 */
static int device_status_cb (axidraw_device_t *dev, void *ctx) {
    (void)ctx;
    ebb_status_snapshot_t snapshot;
    if (axidraw_status (dev, &snapshot) != 0)
        return -1;
    fprintf (CMD_OUT, "Статус пристрою:\n");
    fprintf (CMD_OUT, "  Команда активна: %s\n", snapshot.motion.command_active ? "так" : "ні");
    fprintf (CMD_OUT, "  Мотор X активний: %s\n", snapshot.motion.motor1_active ? "так" : "ні");
    fprintf (CMD_OUT, "  Мотор Y активний: %s\n", snapshot.motion.motor2_active ? "так" : "ні");
    fprintf (CMD_OUT, "  FIFO непорожній: %s\n", snapshot.motion.fifo_pending ? "так" : "ні");
    double steps_per_mm
        = (axidraw_device_settings (dev) && axidraw_device_settings (dev)->steps_per_mm > 0.0)
              ? axidraw_device_settings (dev)->steps_per_mm
              : 0.0;
    if (steps_per_mm > 0.0) {
        fprintf (CMD_OUT, "  Позиція X: %.3f мм\n", snapshot.steps_axis1 / steps_per_mm);
        fprintf (CMD_OUT, "  Позиція Y: %.3f мм\n", snapshot.steps_axis2 / steps_per_mm);
    } else {
        fprintf (CMD_OUT, "  Позиція X: (невідомо — steps_per_mm не встановлено)\n");
        fprintf (CMD_OUT, "  Позиція Y: (невідомо — steps_per_mm не встановлено)\n");
    }
    fprintf (CMD_OUT, "  Перо підняте: %s\n", snapshot.pen_up ? "так" : "ні");
    fprintf (CMD_OUT, "  Серво увімкнено: %s\n", snapshot.servo_power ? "так" : "ні");
    fprintf (CMD_OUT, "  Прошивка: %s\n", snapshot.firmware);
    return 0;
}

/**
 * @brief Колбек для запиту координат у кроках/мм.
 */
static int device_position_cb (axidraw_device_t *dev, void *ctx) {
    (void)ctx;
    int32_t steps1 = 0;
    int32_t steps2 = 0;
    if (ebb_query_steps (dev->port, &steps1, &steps2, dev->timeout_ms) != 0) {
        LOGE ("Не вдалося отримати позицію");
        return -1;
    }
    const axidraw_settings_t *cfg = axidraw_device_settings (dev);
    double steps_per_mm = (cfg && cfg->steps_per_mm > 0.0) ? cfg->steps_per_mm : 0.0;
    if (!(steps_per_mm > 0.0)) {
        LOGE ("Коефіцієнт кроків на міліметр не встановлено — не можу конвертувати у міліметри");
        fprintf (
            CMD_OUT,
            "Поточна позиція: %d (axis1 steps), %d (axis2 steps) — відсутній steps_per_mm\n",
            steps1, steps2);
        return 0;
    }
    fprintf (
        CMD_OUT, "Поточна позиція: X=%.3f мм, Y=%.3f мм\n", steps1 / steps_per_mm,
        steps2 / steps_per_mm);
    return 0;
}

/**
 * @brief Колбек для скидання стану пристрою (перо, кроки, мотори).
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
    fprintf (CMD_OUT, "Пристрій скинуто: перо підняте, лічильники очищено, мотори вимкнені\n");
    return 0;
}

/**
 * @brief Колбек для перезавантаження контролера.
 */
static int device_reboot_cb (axidraw_device_t *dev, void *ctx) {
    (void)ctx;
    if (serial_write_line (dev->port, "RB") != 0) {
        LOGE ("Не вдалося надіслати команду перезавантаження");
        return -1;
    }
    fprintf (CMD_OUT, "Перезавантаження контролера ініційовано\n");
    return 0;
}

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    char path[PATH_MAX];
    bool responsive;
    char version[64];
    char detail[128];
    char alias[64];
} device_port_info_t;

typedef struct {
    double paper_w_mm;
    double paper_h_mm;
    double speed_mm_s;
    double accel_mm_s2;
    char alias[64];
    char port[PATH_MAX];
    char profile_model[64];
    char requested_model[64];
    bool auto_selected;
} device_profile_info_t;

static bool ensure_device_profile (
    config_t *cfg, char *inout_alias, size_t alias_len, const char *model, verbose_level_t verbose);

/**
 * @brief Побудувати дружню назву порту з повного шляху.
 *
 * Пробіли замінюються на підкреслення, при відсутності імені використовується `device`.
 */
static void derive_port_alias (const char *path, char *out, size_t out_len) {
    if (!out || out_len == 0)
        return;
    const char *base = path;
    if (path) {
        const char *slash = strrchr (path, '/');
        if (slash && slash[1])
            base = slash + 1;
    }
    if (!base || !*base)
        base = "device";
    size_t len = 0;
    while (base[len] && len + 1 < out_len) {
        char ch = base[len];
        if (isspace ((unsigned char)ch))
            ch = '_';
        out[len] = ch;
        ++len;
    }
    out[len] = '\0';
    if (len == 0 && out_len > 1) {
        out[0] = 'd';
        out[1] = '\0';
    }
}

/**
 * @brief Перевірити, чи порт вже міститься у зібраному списку.
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
 * @brief Додати новий порт до динамічного масиву (з перевіркою на дубль).
 */
static bool
device_port_add (device_port_info_t **ports, size_t *count, size_t *capacity, const char *path) {
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
    derive_port_alias (slot->path, slot->alias, sizeof (slot->alias));
    ++(*count);
    return true;
}

/**
 * @brief Зібрати потенційні порти AxiDraw із файлової системи.
 *
 * @param[out] ports     Динамічний масив з результатами.
 * @param[out] count     Кількість знайдених портів.
 * @param[in,out] capacity Поточна місткість масиву.
 * @return 0 при успіху; -1 при помилці пам'яті.
 */
static int collect_device_ports (device_port_info_t **ports, size_t *count, size_t *capacity) {
    if (!ports || !count || !capacity)
        return -1;

    static const char *patterns[] = { "/dev/serial/by-id/usb-EiBotBoard*",
                                      "/dev/serial/by-id/usb-*-EiBotBoard*",
                                      "/dev/serial/by-id/usb-*04d8*FD92*",
                                      "/dev/serial/by-id/usb-*04D8*FD92*",
                                      "/dev/tty.usbserial-EiBotBoard*",
                                      "/dev/cu.usbserial-EiBotBoard*",
                                      "/dev/cu.usbmodem*",
                                      "/dev/cu.usbserial*",
                                      "/dev/tty.usbmodem*",
                                      "/dev/tty.usbserial*",
                                      "/dev/ttyACM*",
                                      "/dev/ttyUSB*" };

    for (size_t i = 0; i < sizeof (patterns) / sizeof (patterns[0]); ++i) {
        glob_t g;
        memset (&g, 0, sizeof (g));
        int grc = glob (patterns[i], 0, NULL, &g);
        if (grc == 0) {
            for (size_t j = 0; j < g.gl_pathc; ++j) {
                if (!device_port_add (ports, count, capacity, g.gl_pathv[j])) {
                    LOGE ("Недостатньо пам'яті для переліку портів");
                    globfree (&g);
                    return -1;
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
        if (!device_port_add (ports, count, capacity, guessed)) {
            LOGE ("Недостатньо пам'яті для переліку портів");
            return -1;
        }
    }
#endif

    return 0;
}

/**
 * @brief Перевірити кожен порт на доступність AxiDraw і зчитати метадані.
 */
static void probe_device_ports (device_port_info_t *ports, size_t count) {
    if (!ports)
        return;
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
            strncpy (
                ports[i].detail, "немає відповіді від контролера", sizeof (ports[i].detail) - 1);
            ports[i].detail[sizeof (ports[i].detail) - 1] = '\0';
        }
        serial_close (sp);
    }
}

/**
 * @brief Визначити фактичний порт AxiDraw за псевдонімом або шляхом.
 *
 * @param alias          Псевдонім або NULL.
 * @param[out] port_buf  Буфер для шляху порту.
 * @param port_buf_len   Розмір буфера порту.
 * @param[out] alias_buf Буфер для зворотного псевдоніма (може бути NULL).
 * @param alias_buf_len  Розмір буфера псевдоніма.
 * @param[out] auto_selected Чи було обрано перший доступний порт автоматично.
 * @return 0 успіх; 1 — портів не знайдено; 2 — псевдонім не знайдено; -1 — помилка.
 */
static int resolve_device_port (
    const char *alias,
    char *out_path,
    size_t out_len,
    char *out_alias,
    size_t out_alias_len,
    bool *out_auto_selected) {
    if (!out_path || out_len == 0)
        return -1;

    device_port_info_t *ports = NULL;
    size_t count = 0;
    size_t capacity = 0;
    if (collect_device_ports (&ports, &count, &capacity) != 0) {
        free (ports);
        return -1;
    }
    if (count == 0) {
        free (ports);
        return 1;
    }

    probe_device_ports (ports, count);

    const device_port_info_t *selected = NULL;
    bool requested_alias = alias && *alias;
    if (alias && *alias) {
        for (size_t i = 0; i < count; ++i) {
            if (strcasecmp (alias, ports[i].alias) == 0 || strcmp (alias, ports[i].path) == 0) {
                selected = &ports[i];
                break;
            }
        }
        if (!selected) {
            free (ports);
            return 2;
        }
    } else {
        for (size_t i = 0; i < count; ++i) {
            if (ports[i].responsive) {
                selected = &ports[i];
                break;
            }
        }
        if (!selected)
            selected = &ports[0];
    }

    strncpy (out_path, selected->path, out_len - 1);
    out_path[out_len - 1] = '\0';
    if (out_alias && out_alias_len > 0) {
        strncpy (out_alias, selected->alias, out_alias_len - 1);
        out_alias[out_alias_len - 1] = '\0';
    }
    if (out_auto_selected)
        *out_auto_selected = !requested_alias;
    free (ports);
    return 0;
}

/**
 * @brief Перелічити локальні порти AxiDraw із коротким описом.
 */
static int device_list_local (const char *model, verbose_level_t verbose) {
    (void)model;
    (void)verbose;
    LOGI ("Перелік доступних портів");
    log_print (LOG_INFO, "device.list: модель=%s verbose=%d", model ? model : "<типова>", verbose);

    device_port_info_t *ports = NULL;
    size_t count = 0;
    size_t capacity = 0;

    if (collect_device_ports (&ports, &count, &capacity) != 0) {
        free (ports);
        return 1;
    }

    if (count == 0) {
        fprintf (
            CMD_OUT, "Потенційних портів AxiDraw не знайдено. Підключіть пристрій і повторіть.\n");
        free (ports);
        return 0;
    }

    probe_device_ports (ports, count);

    fprintf (CMD_OUT, "Знайдені порти (%zu):\n", count);
    for (size_t i = 0; i < count; ++i) {
        const char *version = ports[i].version[0] ? ports[i].version : "невідома";
        if (ports[i].responsive) {
            fprintf (
                CMD_OUT, "  - %s (порт: %s) — контролер відповідає (версія %s)\n", ports[i].alias,
                ports[i].path, version);
        } else if (ports[i].detail[0]) {
            fprintf (
                CMD_OUT, "  - %s (порт: %s) — %s\n", ports[i].alias, ports[i].path,
                ports[i].detail);
        } else {
            fprintf (
                CMD_OUT, "  - %s (порт: %s) — без відповіді від AxiDraw\n", ports[i].alias,
                ports[i].path);
        }
    }

    free (ports);
    fprintf (
        CMD_OUT,
        "\nВикористайте 'cplot config set device-name=<назва>', щоб обрати типовий пристрій.\n");
    return 0;
}

/**
 * @brief Зібрати інформацію про профіль пристрою з локального середовища.
 */
static bool device_profile_local (
    const char *alias, const char *model, device_profile_info_t *info, verbose_level_t verbose) {
    if (!info)
        return false;
    memset (info, 0, sizeof (*info));

    char port_buf[PATH_MAX];
    char resolved_alias[64];
    bool auto_selected = false;

    int resolve_rc = resolve_device_port (
        alias && *alias ? alias : NULL, port_buf, sizeof (port_buf), resolved_alias,
        sizeof (resolved_alias), &auto_selected);
    if (resolve_rc == 1) {
        LOGE ("Порт пристрою не знайдено для профілю");
        fprintf (CMD_OUT, "Помилка: пристрої не знайдено\n");
        return false;
    }
    if (resolve_rc == 2) {
        LOGE ("Запитаний псевдонім для профілю не знайдено: %s", alias ? alias : "?");
        fprintf (
            CMD_OUT,
            "Помилка: пристрій із назвою '%s' не знайдено. Запустіть `cplot device list` для "
            "перевірки.\n",
            alias ? alias : "");
        return false;
    }
    if (resolve_rc != 0) {
        LOGE ("Не вдалося визначити порт для профілю");
        fprintf (CMD_OUT, "Помилка: не вдалося визначити порт пристрою\n");
        return false;
    }

    const char *model_id = (model && *model) ? model : CONFIG_DEFAULT_MODEL;
    strncpy (info->requested_model, model_id, sizeof (info->requested_model) - 1);
    info->requested_model[sizeof (info->requested_model) - 1] = '\0';

    const axidraw_device_profile_t *profile = axidraw_device_profile_for_model (model_id);
    if (!profile)
        profile = axidraw_device_profile_default ();
    strncpy (info->profile_model, profile->model, sizeof (info->profile_model) - 1);
    info->profile_model[sizeof (info->profile_model) - 1] = '\0';

    info->paper_w_mm = profile->paper_w_mm;
    info->paper_h_mm = profile->paper_h_mm;
    info->speed_mm_s = profile->speed_mm_s;
    info->accel_mm_s2 = profile->accel_mm_s2;
    strncpy (info->alias, resolved_alias, sizeof (info->alias) - 1);
    info->alias[sizeof (info->alias) - 1] = '\0';
    strncpy (info->port, port_buf, sizeof (info->port) - 1);
    info->port[sizeof (info->port) - 1] = '\0';
    info->auto_selected = auto_selected;

    if (verbose == VERBOSE_ON && info->alias[0]) {
        fprintf (
            CMD_OUT, "Обрано пристрій: %s (%s)%s\n", info->alias,
            info->port[0] ? info->port : "невідомий порт", info->auto_selected ? " [auto]" : "");
    }

    return true;
}

/**
 * @brief Виконати підкоманду `device` у локальному режимі.
 */
/* ---- Табличні ранери дій пристрою (file-scope) ---------------------------------- */
#if 0
static __thread const char *g_device_alias_ctx = NULL;

static int run_device_list (
    const device_action_t *a,
    const char *port,
    const char *model,
    double dx,
    double dy,
    verbose_level_t v) {
    (void)a;
    (void)port;
    (void)dx;
    (void)dy;
    return device_list_local (model, v);
}

static int run_device_profile (
    const device_action_t *a,
    const char *port,
    const char *model,
    double dx,
    double dy,
    verbose_level_t v) {
    (void)a;
    (void)port;
    (void)dx;
    (void)dy;
    device_profile_info_t info;
    /* Використовуємо alias з контексту (g_device_alias_ctx), бо профіль не потребує відкритого
     * порту */
    if (!device_profile_local (g_device_alias_ctx, model, &info, v))
        return 1;
    fprintf (CMD_OUT, "ALIAS=%s\n", info.alias);
    fprintf (CMD_OUT, "PORT=%s\n", info.port);
    fprintf (CMD_OUT, "AUTO_SELECTED=%d\n", info.auto_selected ? 1 : 0);
    fprintf (CMD_OUT, "MODEL_REQUESTED=%s\n", info.requested_model);
    fprintf (CMD_OUT, "PROFILE_MODEL=%s\n", info.profile_model);
    fprintf (CMD_OUT, "PAPER_W_MM=%.3f\n", info.paper_w_mm);
    fprintf (CMD_OUT, "PAPER_H_MM=%.3f\n", info.paper_h_mm);
    fprintf (CMD_OUT, "SPEED_MM_S=%.3f\n", info.speed_mm_s);
    fprintf (CMD_OUT, "ACCEL_MM_S2=%.3f\n", info.accel_mm_s2);
    return 0;
}

static int run_device_pen (
    const device_action_t *a,
    const char *port,
    const char *model,
    double dx,
    double dy,
    verbose_level_t v) {
    (void)dx;
    (void)dy;
    switch (a->pen) {
    case DEVICE_PEN_UP:
        return with_axidraw_device (port, model, v, "підйом пера", device_pen_up_cb, NULL, true);
    case DEVICE_PEN_DOWN:
        return with_axidraw_device (
            port, model, v, "опускання пера", device_pen_down_cb, NULL, true);
    case DEVICE_PEN_TOGGLE:
        return with_axidraw_device (
            port, model, v, "перемикання пера", device_pen_toggle_cb, NULL, true);
    default:
        return 2;
    }
}

static int run_device_motors (
    const device_action_t *a,
    const char *port,
    const char *model,
    double dx,
    double dy,
    verbose_level_t v) {
    (void)dx;
    (void)dy;
    switch (a->motor) {
    case DEVICE_MOTOR_ON:
        return with_axidraw_device (
            port, model, v, "увімкнення моторів", device_motors_on_cb, NULL, false);
    case DEVICE_MOTOR_OFF:
        return with_axidraw_device (
            port, model, v, "вимкнення моторів", device_motors_off_cb, NULL, false);
    default:
        return 2;
    }
}

static int run_device_abort (
    const device_action_t *a,
    const char *port,
    const char *model,
    double dx,
    double dy,
    verbose_level_t v) {
    (void)a;
    (void)dx;
    (void)dy;
    return with_axidraw_device (port, model, v, "аварійна зупинка", device_abort_cb, NULL, false);
}

static int run_device_home (
    const device_action_t *a,
    const char *port,
    const char *model,
    double dx,
    double dy,
    verbose_level_t v) {
    (void)a;
    (void)dx;
    (void)dy;
    return with_axidraw_device (port, model, v, "home", device_home_cb, NULL, true);
}

static int run_device_jog (
    const device_action_t *a,
    const char *port,
    const char *model,
    double dx,
    double dy,
    verbose_level_t v) {
    (void)a;
    jog_ctx_t ctx = { .dx_mm = dx, .dy_mm = dy };
    return with_axidraw_device (port, model, v, "ручний зсув", device_jog_cb, &ctx, false);
}

static int run_device_version (
    const device_action_t *a,
    const char *port,
    const char *model,
    double dx,
    double dy,
    verbose_level_t v) {
    (void)a;
    (void)dx;
    (void)dy;
    return with_axidraw_device (port, model, v, "версія", device_version_cb, NULL, false);
}

static int run_device_status (
    const device_action_t *a,
    const char *port,
    const char *model,
    double dx,
    double dy,
    verbose_level_t v) {
    (void)a;
    (void)dx;
    (void)dy;
    return with_axidraw_device (port, model, v, "статус", device_status_cb, NULL, false);
}

static int run_device_position (
    const device_action_t *a,
    const char *port,
    const char *model,
    double dx,
    double dy,
    verbose_level_t v) {
    (void)a;
    (void)dx;
    (void)dy;
    return with_axidraw_device (port, model, v, "позиція", device_position_cb, NULL, false);
}

static int run_device_reset (
    const device_action_t *a,
    const char *port,
    const char *model,
    double dx,
    double dy,
    verbose_level_t v) {
    (void)a;
    (void)dx;
    (void)dy;
    return with_axidraw_device (port, model, v, "скидання", device_reset_cb, NULL, false);
}

static int run_device_reboot (
    const device_action_t *a,
    const char *port,
    const char *model,
    double dx,
    double dy,
    verbose_level_t v) {
    (void)a;
    (void)dx;
    (void)dy;
    return with_axidraw_device (port, model, v, "перезавантаження", device_reboot_cb, NULL, false);
}
#endif /* legacy CLI dispatchers using device_action_t */
/* ---------------------------------------------------------------------------- */

#if 0
static int device_execute_local (
    const device_action_t *action,
    const char *alias,
    const char *model,
    double jog_dx_mm,
    double jog_dy_mm,
    verbose_level_t verbose) {
    if (!action || action->kind == DEVICE_ACTION_NONE)
        return 2;

    char port_buf[PATH_MAX];
    const char *port = NULL;
    if (action->kind != DEVICE_ACTION_LIST && action->kind != DEVICE_ACTION_PROFILE) {
        const char *requested = (alias && *alias) ? alias : NULL;
        int resolve_rc
            = resolve_device_port (requested, port_buf, sizeof (port_buf), NULL, 0, NULL);
        if (resolve_rc == 1) {
            LOGE ("Порт пристрою не знайдено");
            fprintf (CMD_OUT, "Пристрої не знайдено. Підключіть пристрій і повторіть.\n");
            return 1;
        }
        if (resolve_rc == 2) {
            LOGE ("Запитаний псевдонім пристрою не знайдено: %s", alias ? alias : "?");
            fprintf (
                CMD_OUT,
                "Пристрій із назвою '%s' не знайдено. Запустіть `cplot device list` для "
                "доступних варіантів.\n",
                alias ? alias : "");
            return 1;
        }
        if (resolve_rc != 0) {
            LOGE ("Не вдалося визначити порт пристрою");
            fprintf (CMD_OUT, "Не вдалося визначити порт пристрою.\n");
            return 1;
        }
        port = port_buf;
        LOGI ("локально: використовую порт %s", port);
        log_print (LOG_INFO, "локально: використовую порт %s", port);
    }

    typedef int (*device_action_runner_t) (
        const device_action_t *a, const char *port, const char *model, double jog_dx_mm,
        double jog_dy_mm, verbose_level_t verbose);

    typedef struct {
        device_action_kind_t kind;
        device_action_runner_t runner;
    } device_action_dispatch_t;

    static const device_action_dispatch_t k_device_dispatch[] = {
        { DEVICE_ACTION_LIST, run_device_list },
        { DEVICE_ACTION_PROFILE, run_device_profile },
        { DEVICE_ACTION_PEN, run_device_pen },
        { DEVICE_ACTION_MOTORS, run_device_motors },
        { DEVICE_ACTION_ABORT, run_device_abort },
        { DEVICE_ACTION_HOME, run_device_home },
        { DEVICE_ACTION_JOG, run_device_jog },
        { DEVICE_ACTION_VERSION, run_device_version },
        { DEVICE_ACTION_STATUS, run_device_status },
        { DEVICE_ACTION_POSITION, run_device_position },
        { DEVICE_ACTION_RESET, run_device_reset },
        { DEVICE_ACTION_REBOOT, run_device_reboot },
    };

    const device_action_dispatch_t *entry = NULL;
    for (size_t i = 0; i < sizeof (k_device_dispatch) / sizeof (k_device_dispatch[0]); ++i) {
        if (k_device_dispatch[i].kind == action->kind) {
            entry = &k_device_dispatch[i];
            break;
        }
    }
    if (!entry)
        return 2;

    return entry->runner (action, port, model, jog_dx_mm, jog_dy_mm, verbose);
}
#endif

/**
 * @brief Перетворити рядок у `double` з суворою валідацією.
 */
static bool parse_double_str (const char *value, double *out) {
    if (!value || !out)
        return false;
    char *endptr = NULL;
    double v = strtod (value, &endptr);
    if (endptr == value || (endptr && *endptr))
        return false;
    *out = v;
    return true;
}

/**
 * @brief Перетворити рядок у `int` з суворою валідацією.
 */
static bool parse_int_str (const char *value, int *out) {
    if (!value || !out)
        return false;
    char *endptr = NULL;
    long v = strtol (value, &endptr, 10);
    /* Валідація:
     *  - endptr == value  → не прочитано жодної цифри
     *  - *endptr != '\0'  → залишились сторонні символи
     */
    if (endptr == value || (endptr && *endptr))
        return false;
    /* Перевірка діапазону перед приведенням до int */
    if (v > INT_MAX || v < INT_MIN)
        return false;
    *out = (int)v;
    return true;
}

/**
 * @brief Застосувати одну пару `key=value` до конфігурації.
 *
 * Нормалізує ключ (обрізання пробілів, нижній регістр) і підтримує відомі псевдоніми.
 */
static int config_apply_pair (config_t *cfg, const char *key_raw, const char *value_raw) {
    if (!cfg || !key_raw || !value_raw)
        return -1;

    char key[64];
    strncpy (key, key_raw, sizeof (key) - 1);
    key[sizeof (key) - 1] = '\0';
    string_trim_ascii (key);
    string_to_lower_ascii (key);

    char value_buf[256];
    strncpy (value_buf, value_raw, sizeof (value_buf) - 1);
    value_buf[sizeof (value_buf) - 1] = '\0';
    string_trim_ascii (value_buf);

    if (strcmp (key, "device-name") == 0 || strcmp (key, "device_name") == 0
        || strcmp (key, "default_device") == 0 || strcmp (key, "device") == 0) {
        strncpy (cfg->default_device, value_buf, sizeof (cfg->default_device) - 1);
        cfg->default_device[sizeof (cfg->default_device) - 1] = '\0';
        for (char *p = cfg->default_device; *p; ++p) {
            if (isspace ((unsigned char)*p))
                *p = '_';
        }
        return 0;
    }

    if (strcmp (key, "font-family") == 0 || strcmp (key, "font_family") == 0
        || strcmp (key, "fontfamily") == 0) {
        strncpy (cfg->font_family, value_buf, sizeof (cfg->font_family) - 1);
        cfg->font_family[sizeof (cfg->font_family) - 1] = '\0';
        for (char *p = cfg->font_family; *p; ++p) {
            if (isspace ((unsigned char)*p))
                *p = '_';
        }
        return 0;
    }

    double dbl = 0.0;
    int integer = 0;

    /* width/height/orientation — не зберігаються у конфіг (перемикаються для кожного друку). */
    if (strcmp (key, "paper_w_mm") == 0 || strcmp (key, "paper_w") == 0
        || strcmp (key, "width") == 0)
        return -1;
    if (strcmp (key, "paper_h_mm") == 0 || strcmp (key, "paper_h") == 0
        || strcmp (key, "height") == 0)
        return -1;
    if (strcmp (key, "margin_top_mm") == 0) {
        if (!parse_double_str (value_buf, &dbl))
            return -1;
        cfg->margin_top_mm = dbl;
        return 0;
    }
    if (strcmp (key, "margin_right_mm") == 0) {
        if (!parse_double_str (value_buf, &dbl))
            return -1;
        cfg->margin_right_mm = dbl;
        return 0;
    }
    if (strcmp (key, "margin_bottom_mm") == 0) {
        if (!parse_double_str (value_buf, &dbl))
            return -1;
        cfg->margin_bottom_mm = dbl;
        return 0;
    }
    if (strcmp (key, "margin_left_mm") == 0) {
        if (!parse_double_str (value_buf, &dbl))
            return -1;
        cfg->margin_left_mm = dbl;
        return 0;
    }
    if (strcmp (key, "font_size_pt") == 0 || strcmp (key, "font_size") == 0) {
        if (!parse_double_str (value_buf, &dbl))
            return -1;
        cfg->font_size_pt = dbl;
        return 0;
    }
    if (strcmp (key, "speed_mm_s") == 0 || strcmp (key, "speed") == 0) {
        if (!parse_double_str (value_buf, &dbl))
            return -1;
        cfg->speed_mm_s = dbl;
        return 0;
    }
    if (strcmp (key, "accel_mm_s2") == 0 || strcmp (key, "accel") == 0) {
        if (!parse_double_str (value_buf, &dbl))
            return -1;
        cfg->accel_mm_s2 = dbl;
        return 0;
    }
    /* Положення пера (up/down) — це операційні параметри; не змінюються через config --set. */
    if (strcmp (key, "pen_up_pos") == 0 || strcmp (key, "pen_up") == 0)
        return -1;
    if (strcmp (key, "pen_down_pos") == 0 || strcmp (key, "pen_down") == 0)
        return -1;
    if (strcmp (key, "pen_up_speed") == 0) {
        if (!parse_int_str (value_buf, &integer))
            return -1;
        cfg->pen_up_speed = integer;
        return 0;
    }
    if (strcmp (key, "pen_down_speed") == 0) {
        if (!parse_int_str (value_buf, &integer))
            return -1;
        cfg->pen_down_speed = integer;
        return 0;
    }
    if (strcmp (key, "pen_up_delay_ms") == 0 || strcmp (key, "pen_up_delay") == 0) {
        if (!parse_int_str (value_buf, &integer))
            return -1;
        cfg->pen_up_delay_ms = integer;
        return 0;
    }
    if (strcmp (key, "pen_down_delay_ms") == 0 || strcmp (key, "pen_down_delay") == 0) {
        if (!parse_int_str (value_buf, &integer))
            return -1;
        cfg->pen_down_delay_ms = integer;
        return 0;
    }
    if (strcmp (key, "servo_timeout_s") == 0 || strcmp (key, "servo_timeout") == 0) {
        if (!parse_int_str (value_buf, &integer))
            return -1;
        cfg->servo_timeout_s = integer;
        return 0;
    }
    if (strcmp (key, "orientation") == 0 || strcmp (key, "orient") == 0)
        return -1;

    if (strcmp (key, "margin") == 0 || strcmp (key, "margins") == 0) {
        if (!parse_double_str (value_buf, &dbl))
            return -1;
        cfg->margin_top_mm = cfg->margin_right_mm = cfg->margin_bottom_mm = cfg->margin_left_mm
            = dbl;
        return 0;
    }

    return -1;
}

/**
 * @brief Гарантувати, що конфігурація містить розміри й швидкості для активного пристрою.
 *
 * За потреби зчитує профіль пристрою і оновлює конфігурацію та псевдонім.
 *
 * @param[in,out] cfg        Конфігурація, яку слід доповнити.
 * @param[in,out] inout_alias Буфер з псевдонімом (може бути NULL).
 * @param alias_len          Розмір буфера псевдоніма.
 * @param model              Переважна модель (може бути NULL).
 * @param verbose            Режим детального логування.
 * @return `true`, якщо профіль застосовано або вже був повним.
 */
static bool ensure_device_profile (
    config_t *cfg,
    char *inout_alias,
    size_t alias_len,
    const char *model,
    verbose_level_t verbose) {
    if (!cfg)
        return false;

    if (cfg->paper_w_mm > 0.0 && cfg->paper_h_mm > 0.0 && cfg->speed_mm_s > 0.0
        && cfg->accel_mm_s2 > 0.0)
        return true;

    device_profile_info_t info;
    const char *requested_alias = NULL;
    if (inout_alias && inout_alias[0])
        requested_alias = inout_alias;
    else if (cfg->default_device[0])
        requested_alias = cfg->default_device;

    if (!device_profile_local (requested_alias, model, &info, verbose))
        return false;

    cfg->paper_w_mm = info.paper_w_mm;
    cfg->paper_h_mm = info.paper_h_mm;
    cfg->speed_mm_s = info.speed_mm_s;
    cfg->accel_mm_s2 = info.accel_mm_s2;

    if (info.alias[0]) {
        if (inout_alias && alias_len > 0) {
            strncpy (inout_alias, info.alias, alias_len - 1);
            inout_alias[alias_len - 1] = '\0';
        }
        if (cfg->default_device[0] == '\0' || info.auto_selected) {
            strncpy (cfg->default_device, info.alias, sizeof (cfg->default_device) - 1);
            cfg->default_device[sizeof (cfg->default_device) - 1] = '\0';
        }
    }

    return true;
}

/**
 * @brief Підготувати структуру сторінки для друку з урахуванням моделі пристрою.
 *
 * Застосовує значення за замовчуванням для паперу та полів, якщо користувач їх не вказав.
 *
 * @param[out] page        Структура сторінки для заповнення (не NULL).
 * @param device_model     Ідентифікатор моделі (може бути NULL).
 * @param paper_w_mm       Бажана ширина паперу або ≤0 для значення за замовчуванням.
 * @param paper_h_mm       Бажана висота паперу або ≤0 для значення за замовчуванням.
 * @param margin_top_mm    Верхнє поле або <0 для дефолту.
 * @param margin_right_mm  Праве поле або <0 для дефолту.
 * @param margin_bottom_mm Нижнє поле або <0 для дефолту.
 * @param margin_left_mm   Ліве поле або <0 для дефолту.
 * @param orientation      Орієнтація (портрет/ландшафт) або інше значення для дефолту.
 * @return 0 при успіху; 1 — не вдалося завантажити профіль; 2 — некоректні параметри.
 */
static int build_print_page (
    drawing_page_t *page,
    const char *device_model,
    double paper_w_mm,
    double paper_h_mm,
    double margin_top_mm,
    double margin_right_mm,
    double margin_bottom_mm,
    double margin_left_mm,
    int orientation) {
    if (!page)
        return 1;
    const char *model = (device_model && *device_model) ? device_model : CONFIG_DEFAULT_MODEL;
    config_t cfg;
    if (config_factory_defaults (&cfg, model) != 0) {
        LOGE ("Не вдалося отримати профіль моделі %s", model);
        return 1;
    }

    double final_paper_w = (paper_w_mm > 0.0) ? paper_w_mm : cfg.paper_w_mm;
    double final_paper_h = (paper_h_mm > 0.0) ? paper_h_mm : cfg.paper_h_mm;
    double final_margin_top = (margin_top_mm >= 0.0) ? margin_top_mm : cfg.margin_top_mm;
    double final_margin_right = (margin_right_mm >= 0.0) ? margin_right_mm : cfg.margin_right_mm;
    double final_margin_bottom
        = (margin_bottom_mm >= 0.0) ? margin_bottom_mm : cfg.margin_bottom_mm;
    double final_margin_left = (margin_left_mm >= 0.0) ? margin_left_mm : cfg.margin_left_mm;
    orientation_t final_orientation
        = (orientation == ORIENT_PORTRAIT || orientation == ORIENT_LANDSCAPE)
              ? (orientation_t)orientation
              : cfg.orientation;

    page->paper_w_mm = final_paper_w;
    page->paper_h_mm = final_paper_h;
    page->margin_top_mm = final_margin_top;
    page->margin_right_mm = final_margin_right;
    page->margin_bottom_mm = final_margin_bottom;
    page->margin_left_mm = final_margin_left;
    page->orientation = final_orientation;

    if (!(page->paper_w_mm > 0.0) || !(page->paper_h_mm > 0.0)) {
        LOGE ("Не задано розміри паперу — оберіть активний пристрій (`cplot device profile`)");
        return 2;
    }
    if (page->margin_top_mm < 0.0 || page->margin_right_mm < 0.0 || page->margin_bottom_mm < 0.0
        || page->margin_left_mm < 0.0) {
        LOGE ("Поля не можуть бути від'ємними");
        return 2;
    }
    if (page->margin_left_mm + page->margin_right_mm >= page->paper_w_mm
        || page->margin_top_mm + page->margin_bottom_mm >= page->paper_h_mm) {
        LOGE ("Некоректні поля — робоча область відсутня");
        return 2;
    }
    return 0;
}

/**
 * @brief Виконати команду `print`, побудувавши траєкторію без відправки на пристрій.
 *
 * Поки що функція лише валідує й будує розкладку (майбутня інтеграція з планером).
 *
 * @param in             Вхідний текст у UTF-8.
 * @param font_family    Назва шрифту або NULL.
 * @param font_size_pt   Кегль у пунктах (>0 для перевизначення).
 * @param device_model   Ідентифікатор моделі пристрою або NULL.
 * @param paper_w_mm     Ширина паперу у мм.
 * @param paper_h_mm     Висота паперу у мм.
 * @param margin_top_mm  Верхнє поле у мм.
 * @param margin_right_mm Праве поле у мм.
 * @param margin_bottom_mm Нижнє поле у мм.
 * @param margin_left_mm  Ліве поле у мм.
 * @param orientation    Орієнтація (портрет/ландшафт).
 * @param dry_run        Чи виконувати без фактичної відправки.
 * @param verbose        Режим логування.
 * @return Код виконання (0 → успіх).
 */
/* Побудова даних прев’ю винесена у буферні API (див. нижче). */

static int layout_to_bytes (
    const drawing_layout_t *layout, preview_fmt_t format, uint8_t **out_bytes, size_t *out_len) {
    if (!out_bytes || !out_len)
        return 1;
    *out_bytes = NULL;
    *out_len = 0;
    bytes_t out = { 0 };
    int rc = (format == PREVIEW_FMT_PNG) ? png_render_layout (layout, &out)
                                         : svg_render_layout (layout, &out);
    if (rc == 0 && out.bytes) {
        *out_bytes = out.bytes;
        *out_len = out.len;
    }
    return rc;
}

cmd_result_t cmd_print_execute (
    const char *in_chars,
    size_t in_len,
    bool markdown,
    const char *family,
    double font_size,
    const char *model,
    double paper_w,
    double paper_h,
    double margin_top,
    double margin_right,
    double margin_bottom,
    double margin_left,
    int orientation,
    bool dry_run,
    bool verbose) {
    (void)dry_run;
    (void)verbose;
    if (!in_chars && in_len > 0)
        return 1;
    config_t cfg;
    const char *model_or_null = (model && *model) ? model : NULL;
    config_factory_defaults (&cfg, model_or_null);
    if (!family || (family && *family == '\0'))
        family = (cfg.font_family[0] ? cfg.font_family : NULL);
    if (!(font_size > 0.0))
        font_size = cfg.font_size_pt;
    if (!(paper_w > 0.0))
        paper_w = cfg.paper_w_mm;
    if (!(paper_h > 0.0))
        paper_h = cfg.paper_h_mm;
    if (margin_top < 0.0)
        margin_top = cfg.margin_top_mm;
    if (margin_right < 0.0)
        margin_right = cfg.margin_right_mm;
    if (margin_bottom < 0.0)
        margin_bottom = cfg.margin_bottom_mm;
    if (margin_left < 0.0)
        margin_left = cfg.margin_left_mm;

    drawing_page_t page;
    int setup_rc = build_print_page (
        &page, model, paper_w, paper_h, margin_top, margin_right, margin_bottom, margin_left,
        orientation);
    if (setup_rc != 0)
        return setup_rc;

    string_t input = { .chars = in_chars ? in_chars : "", .len = in_len, .enc = STR_ENC_UTF8 };
    if (markdown) {
        double frame_width_mm
            = (page.orientation == ORIENT_PORTRAIT)
                  ? (page.paper_h_mm - page.margin_top_mm - page.margin_bottom_mm)
                  : (page.paper_w_mm - page.margin_left_mm - page.margin_right_mm);
        markdown_opts_t mopts = { .family = family,
                                  .base_size_pt = (font_size > 0.0 ? font_size : 14.0),
                                  .frame_width_mm = frame_width_mm };
        geom_paths_t md_paths;
        if (markdown_render_paths (input.chars, &mopts, &md_paths, NULL) != 0)
            return 1;
        drawing_layout_t layout_info;
        if (drawing_build_layout_from_paths (&page, &md_paths, &layout_info) != 0) {
            geom_paths_free (&md_paths);
            return 1;
        }
        geom_paths_free (&md_paths);
        drawing_layout_dispose (&layout_info);
        return 0;
    } else {
        drawing_layout_t layout_info = { 0 };
        if (drawing_build_layout (&page, family, font_size, input, &layout_info) != 0)
            return 1;
        drawing_layout_dispose (&layout_info);
        return 0;
    }
}

cmd_result_t cmd_print_preview (
    const char *in_chars,
    size_t in_len,
    bool markdown,
    const char *family,
    double font_size,
    const char *model,
    double paper_w,
    double paper_h,
    double margin_top,
    double margin_right,
    double margin_bottom,
    double margin_left,
    int orientation,
    int preview_png,
    bool verbose,
    uint8_t **out_bytes,
    size_t *out_len) {
    (void)verbose;
    config_t cfg;
    const char *model_or_null = (model && *model) ? model : NULL;
    config_factory_defaults (&cfg, model_or_null);
    if (!family || (family && *family == '\0'))
        family = (cfg.font_family[0] ? cfg.font_family : NULL);
    if (!(font_size > 0.0))
        font_size = cfg.font_size_pt;
    if (!(paper_w > 0.0))
        paper_w = cfg.paper_w_mm;
    if (!(paper_h > 0.0))
        paper_h = cfg.paper_h_mm;
    if (margin_top < 0.0)
        margin_top = cfg.margin_top_mm;
    if (margin_right < 0.0)
        margin_right = cfg.margin_right_mm;
    if (margin_bottom < 0.0)
        margin_bottom = cfg.margin_bottom_mm;
    if (margin_left < 0.0)
        margin_left = cfg.margin_left_mm;

    drawing_page_t page;
    int setup_rc = build_print_page (
        &page, model, paper_w, paper_h, margin_top, margin_right, margin_bottom, margin_left,
        orientation);
    if (setup_rc != 0)
        return setup_rc;

    string_t input = { .chars = in_chars ? in_chars : "", .len = in_len, .enc = STR_ENC_UTF8 };
    int rc = 0;
    preview_fmt_t format = preview_png ? PREVIEW_FMT_PNG : PREVIEW_FMT_SVG;
    if (markdown) {
        double frame_width_mm
            = (page.orientation == ORIENT_PORTRAIT)
                  ? (page.paper_h_mm - page.margin_top_mm - page.margin_bottom_mm)
                  : (page.paper_w_mm - page.margin_left_mm - page.margin_right_mm);
        markdown_opts_t mopts = { .family = family,
                                  .base_size_pt = (font_size > 0.0 ? font_size : 14.0),
                                  .frame_width_mm = frame_width_mm };
        geom_paths_t md_paths;
        if (markdown_render_paths (input.chars, &mopts, &md_paths, NULL) != 0)
            return 1;
        drawing_layout_t layout_info;
        if (drawing_build_layout_from_paths (&page, &md_paths, &layout_info) != 0) {
            geom_paths_free (&md_paths);
            return 1;
        }
        geom_paths_free (&md_paths);
        rc = layout_to_bytes (&layout_info, format, out_bytes, out_len);
        drawing_layout_dispose (&layout_info);
    } else {
        drawing_layout_t layout_info = { 0 };
        if (drawing_build_layout (&page, family, font_size, input, &layout_info) != 0)
            return 1;
        rc = layout_to_bytes (&layout_info, format, out_bytes, out_len);
        drawing_layout_dispose (&layout_info);
    }
    return rc;
}

/**
 * @brief Надрукувати версію програми.
 */
cmd_result_t cmd_version_execute (bool verbose) {
    if (verbose)
        LOGI ("Докладний режим виводу");
    fprintf (CMD_OUT, "%s %s — %s\n", __PROGRAM_NAME__, __PROGRAM_VERSION__, __PROGRAM_AUTHOR__);
    return 0;
}

/**
 * @brief Виконати команду `fonts` (перелік доступних шрифтів).
 */
/* Залишено для сумісності, але викличемо нову реалізацію з прапорцем families_only=0. */
cmd_result_t cmd_fonts_execute (bool verbose) { return cmd_font_list_execute (0, verbose); }

/**
 * @brief Виконати підкоманду `config` (show/reset/set).
 */
cmd_result_t
cmd_config_execute (int action, const char *set_pairs, config_t *inout_cfg, bool verbose) {
    config_t local;
    if (!inout_cfg) {
        if (config_load (&local) != 0)
            config_factory_defaults (&local, CONFIG_DEFAULT_MODEL);
        inout_cfg = &local;
    }
    switch (action) {
    case 0:
        LOGW ("Не вказано дію для налаштувань");
        return 2;
    case 1:
        return cmd_config_show (inout_cfg, verbose);
    case 2:
        return cmd_config_reset (inout_cfg, verbose);
    case 3:
        return cmd_config_set (set_pairs ? set_pairs : "", inout_cfg, verbose);
    default:
        LOGW ("Невідома дія для налаштувань");
        return 2;
    }
}

/**
 * @brief Вивести діагностичну інформацію про систему.
 */

/**
 * @brief Вивести зареєстровані гарнітури Hershey.
 */
cmd_result_t cmd_font_list_execute (bool families_only, bool verbose) {
    (void)verbose;
    if (!families_only) {
        LOGI ("Перелік доступних шрифтів");
        log_print (LOG_INFO, "шрифти: перелік (докладно=%d)", verbose);
        font_face_t *faces = NULL;
        size_t count = 0;
        int rc = fontreg_list (&faces, &count);
        if (rc != 0) {
            fprintf (CMD_OUT, "Не вдалося завантажити реєстр шрифтів (код %d)\n", rc);
            return 1;
        }
        fprintf (CMD_OUT, "Доступні шрифти (%zu):\n", count);
        for (size_t i = 0; i < count; ++i)
            fprintf (CMD_OUT, "  - %-24s — %s\n", faces[i].id, faces[i].name);
        free (faces);
        return 0;
    } else {
        LOGI ("Перелік родин шрифтів");
        font_family_name_t *families = NULL;
        size_t fam_count = 0;
        int rc = fontreg_list_families (&families, &fam_count);
        if (rc != 0) {
            fprintf (CMD_OUT, "Не вдалося завантажити родини шрифтів (код %d)\n", rc);
            return 1;
        }
        fprintf (CMD_OUT, "Доступні родини (%zu):\n", fam_count);
        for (size_t i = 0; i < fam_count; ++i)
            fprintf (CMD_OUT, "  - %-24s — %s\n", families[i].key, families[i].name);
        free (families);
        return 0;
    }
}

/**
 * @brief Вивести поточну конфігурацію `cplot`.
 */
cmd_result_t cmd_config_show (const config_t *cfg, bool verbose) {
    config_t local;
    if (!cfg) {
        if (config_load (&local) != 0)
            config_factory_defaults (&local, CONFIG_DEFAULT_MODEL);
        cfg = &local;
    }
    LOGI ("Поточні налаштування");
    log_print (LOG_INFO, "конфігурація: показ (докладно=%d)", verbose);
    fprintf (CMD_OUT, "Конфігурація cplot:\n");
    fprintf (
        CMD_OUT, "  orientation       : %d (%s)\n", cfg->orientation,
        cfg->orientation == ORIENT_LANDSCAPE ? "landscape" : "portrait");
    fprintf (CMD_OUT, "  paper_w_mm       : %.2f\n", cfg->paper_w_mm);
    fprintf (CMD_OUT, "  paper_h_mm       : %.2f\n", cfg->paper_h_mm);
    fprintf (
        CMD_OUT, "  margins_mm       : top %.2f, right %.2f, bottom %.2f, left %.2f\n",
        cfg->margin_top_mm, cfg->margin_right_mm, cfg->margin_bottom_mm, cfg->margin_left_mm);
    fprintf (CMD_OUT, "  font_size_pt     : %.2f\n", cfg->font_size_pt);
    fprintf (
        CMD_OUT, "  font_family      : %s\n", cfg->font_family[0] ? cfg->font_family : "<типова>");
    fprintf (CMD_OUT, "  speed_mm_s       : %.2f\n", cfg->speed_mm_s);
    fprintf (CMD_OUT, "  accel_mm_s2      : %.2f\n", cfg->accel_mm_s2);
    fprintf (CMD_OUT, "  pen_up_pos       : %d\n", cfg->pen_up_pos);
    fprintf (CMD_OUT, "  pen_down_pos     : %d\n", cfg->pen_down_pos);
    fprintf (CMD_OUT, "  pen_up_speed     : %d\n", cfg->pen_up_speed);
    fprintf (CMD_OUT, "  pen_down_speed   : %d\n", cfg->pen_down_speed);
    fprintf (CMD_OUT, "  pen_up_delay_ms  : %d\n", cfg->pen_up_delay_ms);
    fprintf (CMD_OUT, "  pen_down_delay_ms: %d\n", cfg->pen_down_delay_ms);
    fprintf (CMD_OUT, "  servo_timeout_s  : %d\n", cfg->servo_timeout_s);
    fprintf (
        CMD_OUT, "  default_device   : %s\n",
        cfg->default_device[0] ? cfg->default_device : "<не задано>");
    return 0;
}

/**
 * @brief Скинути конфігурацію до заводських налаштувань поточної моделі.
 */
cmd_result_t cmd_config_reset (config_t *inout_cfg, bool verbose) {
    config_t local;
    if (!inout_cfg) {
        if (config_load (&local) != 0)
            config_factory_defaults (&local, CONFIG_DEFAULT_MODEL);
        inout_cfg = &local;
    }
    LOGI ("Скидання налаштувань");
    log_print (LOG_INFO, "конфігурація: скидання (докладно=%d)", verbose);
    char alias_buf[sizeof (inout_cfg->default_device)];
    strncpy (alias_buf, inout_cfg->default_device, sizeof (alias_buf) - 1);
    alias_buf[sizeof (alias_buf) - 1] = '\0';

    config_factory_defaults (inout_cfg, CONFIG_DEFAULT_MODEL);

    if (!ensure_device_profile (inout_cfg, alias_buf, sizeof (alias_buf), NULL, verbose))
        return 1;

    if (config_save (inout_cfg) != 0) {
        fprintf (CMD_OUT, "Не вдалося зберегти конфігурацію після скидання\n");
        return 1;
    }
    fprintf (CMD_OUT, "Налаштування скинуто до типових значень.\n");
    return 0;
}

/**
 * @brief Застосувати набір `key=value` до конфігурації.
 */
cmd_result_t cmd_config_set (const char *set_pairs, config_t *inout_cfg, bool verbose) {
    config_t local;
    if (!inout_cfg) {
        if (config_load (&local) != 0)
            config_factory_defaults (&local, CONFIG_DEFAULT_MODEL);
        inout_cfg = &local;
    }
    if (!set_pairs || !*set_pairs)
        return 1;
    LOGI ("Застосування нових налаштувань");
    log_print (
        LOG_INFO, "конфігурація: встановлення (%s)",
        set_pairs && *set_pairs ? set_pairs : "<порожньо>");
    (void)verbose;

    char *mutable_pairs = strdup (set_pairs);
    if (!mutable_pairs)
        return 1;

    int status = 0;
    char *saveptr = NULL;
    for (char *item = strtok_r (mutable_pairs, ",", &saveptr); item;
         item = strtok_r (NULL, ",", &saveptr)) {
        char *eq = strchr (item, '=');
        if (!eq) {
            fprintf (CMD_OUT, "Пропущено запис '%s' (очікується key=value)\n", item);
            status = 1;
            continue;
        }
        *eq = '\0';
        const char *key = item;
        const char *value = eq + 1;
        if (config_apply_pair (inout_cfg, key, value) != 0) {
            fprintf (CMD_OUT, "Невідомий або некоректний ключ: %s\n", key);
            status = 1;
        }
    }

    free (mutable_pairs);

    if (status != 0)
        return 1;

    char errbuf[128];
    if (config_validate (inout_cfg, errbuf, sizeof (errbuf)) != 0) {
        fprintf (
            CMD_OUT, "Конфігурація містить некоректні значення: %s\n",
            errbuf[0] ? errbuf : "невідомо");
        config_load (inout_cfg);
        return 1;
    }

    if (config_save (inout_cfg) != 0) {
        fprintf (CMD_OUT, "Не вдалося зберегти конфігурацію\n");
        config_load (inout_cfg);
        return 1;
    }

    fprintf (CMD_OUT, "Зміни конфігурації збережено.\n");
    return 0;
}

/**
 * @brief Виконати підкоманду `device` (локально).
 *
 * @param action     Опис дії пристрою.
 * @param alias      Псевдонім пристрою (може бути NULL).
 * @param model      Модель AxiDraw (може бути NULL).
 * @param jog_dx_mm  Зсув по X для `jog`.
 * @param jog_dy_mm  Зсув по Y для `jog`.
 * @param verbose    Режим логування.
 * @return Код виконання (0 → успіх, 2 → невідома дія тощо).
 */
/* ---- Публічні функції пристрою без "actions" ------------------------------ */
cmd_result_t cmd_device_list (const char *model, bool verbose) {
    return device_list_local (model, verbose ? VERBOSE_ON : VERBOSE_OFF);
}

cmd_result_t cmd_device_profile (const char *alias, const char *model, bool verbose) {
    device_profile_info_t info;
    if (!device_profile_local (alias, model, &info, verbose ? VERBOSE_ON : VERBOSE_OFF))
        return 1;
    fprintf (CMD_OUT, "ALIAS=%s\n", info.alias);
    fprintf (CMD_OUT, "PORT=%s\n", info.port);
    fprintf (CMD_OUT, "AUTO_SELECTED=%d\n", info.auto_selected ? 1 : 0);
    fprintf (CMD_OUT, "MODEL_REQUESTED=%s\n", info.requested_model);
    fprintf (CMD_OUT, "PROFILE_MODEL=%s\n", info.profile_model);
    fprintf (CMD_OUT, "PAPER_W_MM=%.3f\n", info.paper_w_mm);
    fprintf (CMD_OUT, "PAPER_H_MM=%.3f\n", info.paper_h_mm);
    fprintf (CMD_OUT, "SPEED_MM_S=%.3f\n", info.speed_mm_s);
    fprintf (CMD_OUT, "ACCEL_MM_S2=%.3f\n", info.accel_mm_s2);
    return 0;
}

static int resolve_port_or_err (const char *alias, char *out, size_t outlen) {
    int r = resolve_device_port (alias && *alias ? alias : NULL, out, outlen, NULL, 0, NULL);
    if (r == 0)
        return 0;
    if (r == 1)
        fprintf (CMD_OUT, "Пристрої AxiDraw не знайдено. Підключіть пристрій і повторіть.\n");
    else if (r == 2)
        fprintf (CMD_OUT, "Пристрій із назвою '%s' не знайдено.\n", alias ? alias : "");
    return 1;
}

#define DEV_WRAP(name, action_desc, cb, wait_idle)                                                 \
    cmd_result_t name (const char *alias, const char *model, bool verbose) {                       \
        char port_buf[PATH_MAX];                                                                   \
        if (resolve_port_or_err (alias, port_buf, sizeof (port_buf)) != 0)                         \
            return 1;                                                                              \
        return with_axidraw_device (                                                               \
            port_buf, model, verbose ? VERBOSE_ON : VERBOSE_OFF, action_desc, cb, NULL,            \
            wait_idle);                                                                            \
    }

DEV_WRAP (cmd_device_pen_up, "підйом пера", device_pen_up_cb, true)
DEV_WRAP (cmd_device_pen_down, "опускання пера", device_pen_down_cb, true)
DEV_WRAP (cmd_device_pen_toggle, "перемикання пера", device_pen_toggle_cb, true)
DEV_WRAP (cmd_device_motors_on, "увімкнення моторів", device_motors_on_cb, false)
DEV_WRAP (cmd_device_motors_off, "вимкнення моторів", device_motors_off_cb, false)
DEV_WRAP (cmd_device_abort, "аварійна зупинка", device_abort_cb, false)
DEV_WRAP (cmd_device_home, "home", device_home_cb, true)
DEV_WRAP (cmd_device_version, "версія", device_version_cb, false)
DEV_WRAP (cmd_device_status, "статус", device_status_cb, false)
DEV_WRAP (cmd_device_position, "позиція", device_position_cb, false)
DEV_WRAP (cmd_device_reset, "скидання", device_reset_cb, false)
DEV_WRAP (cmd_device_reboot, "перезавантаження", device_reboot_cb, false)

cmd_result_t
cmd_device_jog (const char *alias, const char *model, double dx_mm, double dy_mm, bool verbose) {
    char port_buf[PATH_MAX];
    if (resolve_port_or_err (alias, port_buf, sizeof (port_buf)) != 0)
        return 1;
    jog_ctx_t ctx = { .dx_mm = dx_mm, .dy_mm = dy_mm };
    return with_axidraw_device (
        port_buf, model, verbose ? VERBOSE_ON : VERBOSE_OFF, "ручний зсув", device_jog_cb, &ctx,
        false);
}
