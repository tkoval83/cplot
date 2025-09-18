/**
 * @file cmd.c
 * @brief Реалізації фасаду підкоманд (скорочена назва файлу).
 */
#include "cmd.h"
#include "axidraw.h"
#include "axistate.h"
#include "canvas.h"
#include "config.h"
#include "fontreg.h"
#include "help.h"
#include "hud.h"
#include "log.h"
#include "trace.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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

/** Максимальна тривалість команди SM/XM відповідно до протоколу EBB. */
#define AXIDRAW_MAX_DURATION_MS 16777215u

/** Кількість ітерацій очікування стану спокою після керуючої команди. */
#define AXIDRAW_IDLE_WAIT_ATTEMPTS 200

/**
 * @brief Попередити користувача про зайнятість пристрою іншою сесією.
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
    LOGW ("AxiDraw вже використовується (%s)", holder);
}

/**
 * @brief Додатково залогувати знімок стану при збиранні з DEBUG.
 *
 * @param phase         Етап виконання (before/after/... ).
 * @param have_snapshot Чи успішно отримано дані статусу.
 * @param snap          Знімок статусу або NULL.
 */
#ifdef DEBUG
static void
debug_log_snapshot (const char *phase, bool have_snapshot, const ebb_status_snapshot_t *snap) {
    if (!have_snapshot || !snap) {
        LOGD ("debug[%s]: статус недоступний", phase);
        return;
    }
    LOGD (
        "debug[%s]: active=%d motorX=%d motorY=%d fifo=%d pen_up=%d pos=(%ld,%ld)", phase,
        snap->motion.command_active, snap->motion.motor1_active, snap->motion.motor2_active,
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
 * @brief Оновити глобальний axistate та HUD після команди до пристрою.
 *
 * @param dev       Пристрій (може бути NULL).
 * @param phase     Позначка етапу для axistate.
 * @param action    Опис дії.
 * @param command_rc Код повернення основної команди.
 * @param wait_rc    Код повернення очікування FIFO.
 */
static void update_state_from_device (
    axidraw_device_t *dev, const char *phase, const char *action, int command_rc, int wait_rc) {
    ebb_status_snapshot_t snap;
    bool ok = false;
    if (dev && axidraw_device_is_connected (dev))
        ok = axidraw_status (dev, &snap) == 0;
    debug_log_snapshot (phase, ok, ok ? &snap : NULL);
    axistate_update (phase, action, command_rc, wait_rc, ok ? &snap : NULL);
}

/**
 * @brief Спробувати розпарсити рядок як число з плаваючою крапкою.
 *
 * @param s         Рядок (NULL → помилка).
 * @param out_value Вихідний параметр.
 * @return true, якщо парсинг успішний.
 */
static bool parse_double_str (const char *s, double *out_value) {
    if (!s || !*s || !out_value)
        return false;
    errno = 0;
    char *endptr = NULL;
    double value = strtod (s, &endptr);
    if (errno != 0 || !endptr || *endptr != '\0' || !isfinite (value))
        return false;
    *out_value = value;
    return true;
}

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
    cmd_result_t status = 0;
    int lock_fd = -1;
    if (axidraw_device_lock_acquire (&lock_fd) != 0) {
        warn_device_busy ();
        return 1;
    }

    axidraw_settings_t settings;
    if (!load_axidraw_settings (model, &settings)) {
        LOGE ("Не вдалося завантажити налаштування для моделі %s", model ? model : "(типова)");
        axistate_update ("config_fail", action_name, -1, 0, NULL);
        status = 1;
        goto cleanup;
    }

    axidraw_device_t dev;
    axidraw_device_init (&dev);
    axidraw_apply_settings (&dev, &settings);
    axidraw_device_config (&dev, port, 9600, 5000, settings.min_cmd_interval_ms);

    char errbuf[256];
    if (axidraw_device_connect (&dev, errbuf, sizeof (errbuf)) != 0) {
        LOGE ("Не вдалося підключитися до AxiDraw: %s", errbuf);
        axistate_update ("connect_fail", action_name, -1, 0, NULL);
        axidraw_device_disconnect (&dev);
        status = 1;
        goto cleanup;
    }

    if (verbose == VERBOSE_ON) {
        LOGI ("Виконання дії '%s' на порту %s", action_name, dev.port_path);
    }

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

static int device_abort_cb (axidraw_device_t *dev, void *ctx) {
    (void)ctx;
    int rc = axidraw_emergency_stop (dev);
    if (rc == 0)
        fprintf (stdout, "Аварійна зупинка виконана\n");
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
    fprintf (stdout, "  Команда активна: %s\n", snapshot.motion.command_active ? "так" : "ні");
    fprintf (stdout, "  Мотор X активний: %s\n", snapshot.motion.motor1_active ? "так" : "ні");
    fprintf (stdout, "  Мотор Y активний: %s\n", snapshot.motion.motor2_active ? "так" : "ні");
    fprintf (stdout, "  FIFO непорожній: %s\n", snapshot.motion.fifo_pending ? "так" : "ні");
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
    fprintf (
        stdout, "Поточна позиція: X=%.3f мм, Y=%.3f мм\n", steps1 / AXIDRAW_STEPS_PER_MM,
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
 * @brief Періодично оновлювати HUD стану пристрою.
 *
 * @param interval Інтервал між оновленнями в секундах; значення ≤0 замінюється на 1.0.
 * @param max_iter Кількість оновлень; від’ємне значення означає безкінечний режим.
 */
/**
 * @brief Зчитати поточну позицію осей з axistate/пристрою.
 *
 * @param dev   Пристрій (використовується для оновлення стану).
 * @param x_mm  Вихід: координата X у міліметрах (може бути NULL).
 * @param y_mm  Вихід: координата Y у міліметрах (може бути NULL).
 * @return true, якщо позицію успішно отримано.
 */
static bool shell_get_position (axidraw_device_t *dev, double *x_mm, double *y_mm) {
    update_state_from_device (dev, "query", "query", 0, 0);
    axistate_t state;
    if (!axistate_get (&state) || !state.snapshot_valid)
        return false;
    if (x_mm)
        *x_mm = state.snapshot.steps_axis1 / AXIDRAW_STEPS_PER_MM;
    if (y_mm)
        *y_mm = state.snapshot.steps_axis2 / AXIDRAW_STEPS_PER_MM;
    return true;
}

/**
 * @brief Виконати відносне переміщення у режимі shell.
 *
 * @param dev   Пристрій (не NULL).
 * @param dx_mm Зміщення по X у мм.
 * @param dy_mm Зміщення по Y у мм.
 * @param phase Назва фази для axistate.
 * @param action Назва дії для axistate.
 * @return Код повернення device_jog_cb().
 */
static int shell_move_relative (
    axidraw_device_t *dev, double dx_mm, double dy_mm, const char *phase, const char *action) {
    struct jog_ctx ctx = { .dx_mm = dx_mm, .dy_mm = dy_mm };
    int rc = device_jog_cb (dev, &ctx);
    update_state_from_device (dev, phase, action, rc, 0);
    return rc;
}

/**
 * @brief Повернути суфікс команди після префікса, допускаючи роздільники - або :.
 *
 * Наприклад, для "pen-up" і префікса "pen" повертає "up".
 *
 * @param token   Початковий токен команди.
 * @param prefix  Очікуваний префікс.
 * @return Рядок після роздільника або пустий рядок, якщо префікс збігається без продовження;
 *         NULL, якщо префікс не збігається.
 */
static const char *command_suffix (const char *token, const char *prefix) {
    size_t len = strlen (prefix);
    if (strncasecmp (token, prefix, len) != 0)
        return NULL;
    char c = token[len];
    if (c == '\0')
        return "";
    if (c == '-' || c == ':')
        return token + len + 1;
    return NULL;
}

/**
 * @brief Вивести довідку інтерактивної оболонки.
 */
static void shell_print_help (void) {
    fprintf (stdout, "Доступні команди:\n");
    fprintf (stdout, "  help                 — показати це повідомлення\n");
    fprintf (stdout, "  quit | exit          — завершити інтерактивну сесію\n");
    fprintf (
        stdout,
        "  connect [PORT|auto]  — підключитися (допускається connect:/path або connect:auto)\n");
    fprintf (stdout, "  disconnect           — розірвати з’єднання\n");
    fprintf (
        stdout, "  model [NAME]         — показати/встановити модель (можна model:minikit2)\n");
    fprintf (stdout, "  list                 — перелік потенційних портів AxiDraw\n");
    fprintf (
        stdout, "  pen up|down|toggle   — керування пером (також pen-up, pen:down, pen-toggle)\n");
    fprintf (
        stdout,
        "  motors on|off        — увімкнути/вимкнути мотори (також motors-on, motors:off)\n");
    fprintf (stdout, "  abort                — аварійно зупинити всі рухи\n");
    fprintf (stdout, "  moveto <x> <y>       — переміститися у абсолютні координати (мм)\n");
    fprintf (stdout, "  lineto <x> <y>       — провести лінію до абсолютних координат (мм)\n");
    fprintf (stdout, "  move <dx> <dy>       — зсув на dx/dy мм\n");
    fprintf (stdout, "  line <dx> <dy>       — провести лінію на dx/dy мм (відносно)\n");
    fprintf (stdout, "  jog <dx> <dy>        — ручний зсув на dx/dy мм\n");
    fprintf (stdout, "  home                 — повернутися у початкову позицію\n");
    fprintf (
        stdout, "  reset                — підняти перо, очистити лічильники й вимкнути мотори\n");
    fprintf (stdout, "  reboot               — перезавантажити контролер EBB\n");
}

/**
 * @brief Запустити інтерактивну оболонку керування AxiDraw.
 *
 * @param port     Бажаний серійний порт або NULL для автопошуку.
 * @param model    Модель пристрою або NULL для типової.
 * @param verbose  Рівень деталізації логів.
 * @return 0 у разі успіху; ненульовий код — помилка.
 */
cmd_result_t cmd_device_shell (const char *port, const char *model, verbose_level_t verbose) {
    int lock_fd = -1;
    if (axidraw_device_lock_acquire (&lock_fd) != 0) {
        warn_device_busy ();
        return 1;
    }

    axidraw_device_t dev;
    axidraw_device_init (&dev);
    bool connected = false;
    bool settings_loaded = false;
    axidraw_settings_t settings;

    char current_model[32];
    if (model && *model)
        strncpy (current_model, model, sizeof (current_model) - 1);
    else
        strncpy (current_model, CONFIG_DEFAULT_MODEL, sizeof (current_model) - 1);
    current_model[sizeof (current_model) - 1] = '\0';

    config_t hud_cfg;
    if (config_load (&hud_cfg) != 0)
        config_factory_defaults (&hud_cfg, current_model);

    if (load_axidraw_settings (current_model, &settings)) {
        axidraw_apply_settings (&dev, &settings);
        settings_loaded = true;
    } else {
        LOGE ("Не вдалося завантажити налаштування для моделі %s", current_model);
    }

    hud_set_sources (&dev, settings_loaded ? &settings : NULL, &hud_cfg, current_model);
    hud_reset ();
    hud_render (NULL, true);
    axistate_enable_auto_print (true);

    if (settings_loaded) {
        axidraw_device_config (
            &dev, (port && *port) ? port : NULL, 9600, 5000, settings.min_cmd_interval_ms);
        if (!port || !*port)
            dev.port_path[0] = '\0';
        char errbuf[256];
        if (axidraw_device_connect (&dev, errbuf, sizeof (errbuf)) == 0) {
            connected = true;
            LOGI ("Підключено до AxiDraw через %s", dev.port_path);
            hud_render (NULL, true);
        } else {
            connected = false;
            dev.port_path[0] = '\0';
            LOGW ("Автопідключення не вдалося: %s", errbuf);
            hud_render (NULL, true);
        }
    }

    fprintf (
        stdout,
        "Інтерактивний режим AxiDraw. Введіть 'help' для списку команд, 'quit' для виходу.\n");

    char *line = NULL;
    size_t linecap = 0;
    bool quit = false;
    while (!quit) {
        fprintf (stdout, "cplot> ");
        fflush (stdout);
        ssize_t read = getline (&line, &linecap, stdin);
        if (read < 0)
            break;
        while (read > 0 && (line[read - 1] == '\n' || line[read - 1] == '\r'))
            line[--read] = '\0';

        char *cursor = line;
        while (*cursor && isspace ((unsigned char)*cursor))
            ++cursor;
        if (*cursor == '\0')
            continue;

        /* Лімітуємо кількість токенів до 8, цього достатньо для наших команд. */
        char *tokens[8];
        size_t ntokens = 0;
        char *saveptr = NULL;
        char *tok = strtok_r (cursor, " \t", &saveptr);
        while (tok && ntokens < 8) {
            tokens[ntokens++] = tok;
            tok = strtok_r (NULL, " \t", &saveptr);
        }
        if (ntokens == 0)
            continue;

        const char *cmd = tokens[0];

        if (strcasecmp (cmd, "help") == 0 || strcasecmp (cmd, "?") == 0) {
            shell_print_help ();
            continue;
        }
        if (strcasecmp (cmd, "quit") == 0 || strcasecmp (cmd, "exit") == 0) {
            quit = true;
            continue;
        }
        const char *model_suffix = command_suffix (cmd, "model");
        if (model_suffix) {
            const char *new_model = NULL;
            if (*model_suffix)
                new_model = model_suffix;
            else if (ntokens >= 2)
                new_model = tokens[1];

            if (!new_model) {
                fprintf (stdout, "Поточна модель: %s\n", current_model);
            } else {
                axidraw_settings_t new_settings;
                if (!load_axidraw_settings (new_model, &new_settings)) {
                    LOGE ("Не вдалося завантажити налаштування для моделі %s", new_model);
                } else {
                    strncpy (current_model, new_model, sizeof (current_model) - 1);
                    current_model[sizeof (current_model) - 1] = '\0';
                    settings = new_settings;
                    settings_loaded = true;
                    axidraw_apply_settings (&dev, &settings);
                    config_factory_defaults (&hud_cfg, current_model);
                    hud_set_sources (&dev, &settings, &hud_cfg, current_model);
                    hud_render (NULL, true);
                    if (connected)
                        LOGI ("Налаштування моделі застосовані до активного з’єднання");
                }
            }
            continue;
        }
        if (strcasecmp (cmd, "list") == 0) {
            cmd_device_list (current_model, verbose);
            continue;
        }
        const char *connect_suffix = command_suffix (cmd, "connect");
        if (connect_suffix) {
            const char *req_port = NULL;
            if (*connect_suffix)
                req_port = connect_suffix;
            else if (ntokens >= 2)
                req_port = tokens[1];
            if (!settings_loaded) {
                if (!load_axidraw_settings (current_model, &settings)) {
                    LOGE ("Не вдалося завантажити налаштування для моделі %s", current_model);
                    continue;
                }
                settings_loaded = true;
            }
            axidraw_apply_settings (&dev, &settings);
            if (connected) {
                axidraw_device_disconnect (&dev);
                connected = false;
            }
            if (req_port && strcmp (req_port, "auto") != 0)
                axidraw_device_config (&dev, req_port, 9600, 5000, settings.min_cmd_interval_ms);
            else {
                axidraw_device_config (&dev, NULL, 9600, 5000, settings.min_cmd_interval_ms);
                dev.port_path[0] = '\0';
            }
            char errbuf[256];
            if (axidraw_device_connect (&dev, errbuf, sizeof (errbuf)) == 0) {
                connected = true;
                LOGI ("Підключено через %s", dev.port_path);
                update_state_from_device (&dev, "connect", "connect", 0, 0);
            } else {
                connected = false;
                dev.port_path[0] = '\0';
                LOGE ("Не вдалося підключитися: %s", errbuf);
                axistate_update ("connect_fail", "connect", -1, 0, NULL);
            }
            continue;
        }
        if (strcasecmp (cmd, "disconnect") == 0) {
            if (connected) {
                if (wait_for_device_idle (&dev) != 0)
                    LOGW ("Не вдалося підтвердити завершення команд перед відключенням");
                axidraw_device_disconnect (&dev);
                connected = false;
                LOGI ("З’єднання розірвано");
                axistate_update ("disconnect", "disconnect", 0, 0, NULL);
            } else {
                LOGW ("З’єднання ще не встановлено");
            }
            continue;
        }

        if (!connected) {
            LOGE ("Немає активного з’єднання. Використайте команду connect");
            continue;
        }

        const char *pen_suffix = command_suffix (cmd, "pen");
        if (pen_suffix) {
            const char *action = NULL;
            if (*pen_suffix)
                action = pen_suffix;
            else if (ntokens >= 2)
                action = tokens[1];
            if (!action || !*action) {
                LOGW ("Формат: pen up|down|toggle або pen-up/pen:down");
                continue;
            }
            int rc = 0;
            if (strcasecmp (action, "up") == 0 || strcasecmp (action, "u") == 0)
                rc = device_pen_up_cb (&dev, NULL);
            else if (strcasecmp (action, "down") == 0 || strcasecmp (action, "d") == 0)
                rc = device_pen_down_cb (&dev, NULL);
            else if (
                strcasecmp (action, "toggle") == 0 || strcasecmp (action, "t") == 0
                || strcasecmp (action, "switch") == 0)
                rc = device_pen_toggle_cb (&dev, NULL);
            else {
                LOGW ("Невідомий параметр пера");
                continue;
            }
            if (rc != 0)
                LOGE ("Команда не виконана");
            update_state_from_device (&dev, "pen", action, rc, 0);
            continue;
        }
        const char *motors_suffix = command_suffix (cmd, "motors");
        if (motors_suffix) {
            const char *action = NULL;
            if (*motors_suffix)
                action = motors_suffix;
            else if (ntokens >= 2)
                action = tokens[1];
            if (!action || !*action) {
                LOGW ("Формат: motors on|off або motors-on/motors:off");
                continue;
            }
            int rc = 0;
            if (strcasecmp (action, "on") == 0)
                rc = device_motors_on_cb (&dev, NULL);
            else if (strcasecmp (action, "off") == 0)
                rc = device_motors_off_cb (&dev, NULL);
            else {
                LOGW ("Невідомий параметр моторів");
                continue;
            }
            if (rc != 0)
                LOGE ("Команда не виконана");
            update_state_from_device (&dev, "motors", action, rc, 0);
            continue;
        }
        if (strcasecmp (cmd, "abort") == 0) {
            int rc = device_abort_cb (&dev, NULL);
            if (rc != 0)
                LOGE ("Не вдалося виконати аварійну зупинку");
            update_state_from_device (&dev, "abort", "abort", rc, 0);
            continue;
        }
        if (strcasecmp (cmd, "home") == 0) {
            int rc = device_home_cb (&dev, NULL);
            if (rc != 0)
                LOGE ("Не вдалося повернутися у початкову позицію");
            update_state_from_device (&dev, "home", "home", rc, 0);
            continue;
        }
        if (strcasecmp (cmd, "move") == 0 || strcasecmp (cmd, "line") == 0) {
            if (ntokens < 3) {
                LOGW ("Формат: %s <dx мм> <dy мм>", cmd);
                continue;
            }
            double dx = 0.0;
            double dy = 0.0;
            if (!parse_double_str (tokens[1], &dx) || !parse_double_str (tokens[2], &dy)) {
                LOGW ("Некоректні значення dx/dy");
                continue;
            }
            if (strcasecmp (cmd, "line") == 0) {
                update_state_from_device (&dev, "query", "line", 0, 0);
                axistate_t state;
                if (axistate_get (&state) && state.snapshot_valid && state.snapshot.pen_up)
                    LOGW ("Перo підняте — лінія виконується у повітрі");
            }
            int rc = shell_move_relative (
                &dev, dx, dy, strcasecmp (cmd, "line") == 0 ? "line" : "move", cmd);
            if (rc != 0)
                LOGE ("Не вдалося виконати зсув");
            continue;
        }
        if (strcasecmp (cmd, "moveto") == 0 || strcasecmp (cmd, "lineto") == 0) {
            if (ntokens < 3) {
                LOGW ("Формат: %s <x мм> <y мм>", cmd);
                continue;
            }
            double target_x = 0.0;
            double target_y = 0.0;
            if (!parse_double_str (tokens[1], &target_x)
                || !parse_double_str (tokens[2], &target_y)) {
                LOGW ("Некоректні координати");
                continue;
            }
            double cur_x = 0.0;
            double cur_y = 0.0;
            if (!shell_get_position (&dev, &cur_x, &cur_y)) {
                LOGW ("Не вдалося визначити поточну позицію");
                continue;
            }
            double dx = target_x - cur_x;
            double dy = target_y - cur_y;
            if (fabs (dx) < 1e-3 && fabs (dy) < 1e-3)
                continue;
            if (strcasecmp (cmd, "lineto") == 0) {
                update_state_from_device (&dev, "query", "lineto", 0, 0);
                axistate_t state;
                if (axistate_get (&state) && state.snapshot_valid && state.snapshot.pen_up)
                    LOGW ("Перo підняте — лінія виконується у повітрі");
            }
            int rc = shell_move_relative (
                &dev, dx, dy, strcasecmp (cmd, "lineto") == 0 ? "lineto" : "moveto", cmd);
            if (rc != 0)
                LOGE ("Не вдалося виконати переміщення");
            continue;
        }
        if (strcasecmp (cmd, "jog") == 0) {
            if (ntokens < 3) {
                LOGW ("Формат: jog <dx мм> <dy мм>");
                continue;
            }
            double dx = 0.0;
            double dy = 0.0;
            if (!parse_double_str (tokens[1], &dx) || !parse_double_str (tokens[2], &dy)) {
                LOGW ("Некоректні значення dx/dy");
                continue;
            }
            struct jog_ctx ctx = { .dx_mm = dx, .dy_mm = dy };
            int rc = device_jog_cb (&dev, &ctx);
            if (rc != 0)
                LOGE ("Не вдалося виконати зсув");
            update_state_from_device (&dev, "jog", "jog", rc, 0);
            continue;
        }
        if (strcasecmp (cmd, "reset") == 0) {
            int rc = device_reset_cb (&dev, NULL);
            if (rc != 0)
                LOGE ("Не вдалося скинути стан");
            update_state_from_device (&dev, "reset", "reset", rc, 0);
            continue;
        }
        if (strcasecmp (cmd, "reboot") == 0) {
            int rc = device_reboot_cb (&dev, NULL);
            if (rc != 0) {
                LOGE ("Не вдалося ініціювати перезавантаження");
                update_state_from_device (&dev, "reboot_fail", "reboot", rc, 0);
                continue;
            }
            if (wait_for_device_idle (&dev) != 0)
                LOGW ("Не вдалося підтвердити завершення команд перед перезавантаженням");
            axistate_update ("reboot", "reboot", rc, 0, NULL);
            axidraw_device_disconnect (&dev);
            connected = false;
            settings_loaded = false;
            LOGI ("Контролер перезавантажується — підключіться знову після готовності");
            continue;
        }

        LOGW ("Невідома команда. Використайте 'help'");
    }

    free (line);
    if (connected) {
        if (wait_for_device_idle (&dev) != 0)
            LOGW ("Не вдалося підтвердити завершення команд перед відключенням");
        axidraw_device_disconnect (&dev);
        axistate_update ("disconnect", "disconnect", 0, 0, NULL);
        hud_render (NULL, true);
    }
    axidraw_device_lock_release (lock_fd);
    axistate_enable_auto_print (false);
    LOGI ("Інтерактивну сесію завершено");
    return 0;
}

/**
 * Виконати підкоманду print.
 *
 * Справжня побудова траєкторій буде виконуватися модулем canvas; наразі функція
 * повертає код помилки, оскільки canvas ще не реалізовано.
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
    LOGI ("Почато побудову траєкторії");
    trace_write (
        LOG_INFO,
        "cmd.print: bytes=%zu font=%s папір=%.1fx%.1f поля=%.1f/%.1f/%.1f/%.1f орієнтація=%d "
        "dry=%s verbose=%d",
        in.len, font_family ? font_family : "<типовий>", paper_w_mm, paper_h_mm, margin_top_mm,
        margin_right_mm, margin_bottom_mm, margin_left_mm, orientation, dry_run ? "так" : "ні",
        verbose);

    config_t cfg;
    if (config_factory_defaults (&cfg, CONFIG_DEFAULT_MODEL) != 0) {
        LOGE ("Не вдалося отримати конфігурацію за замовчуванням");
        return 1;
    }

    double effective_w = paper_w_mm > 0.0 ? paper_w_mm : cfg.paper_w_mm;
    double effective_h = paper_h_mm > 0.0 ? paper_h_mm : cfg.paper_h_mm;
    double top_margin = margin_top_mm >= 0.0 ? margin_top_mm : cfg.margin_top_mm;
    double right_margin = margin_right_mm >= 0.0 ? margin_right_mm : cfg.margin_right_mm;
    double bottom_margin = margin_bottom_mm >= 0.0 ? margin_bottom_mm : cfg.margin_bottom_mm;
    double left_margin = margin_left_mm >= 0.0 ? margin_left_mm : cfg.margin_left_mm;
    orientation_t orient = (orientation > 0) ? (orientation_t)orientation : cfg.orientation;

    if (!(effective_w > 0.0) || !(effective_h > 0.0)) {
        LOGE ("Не задано розміри паперу");
        return 2;
    }

    double canvas_w = effective_w;
    double canvas_h = effective_h;
    if (orient == ORIENT_LANDSCAPE) {
        canvas_w = effective_h;
        canvas_h = effective_w;
    }

    double x0 = left_margin;
    double y0 = top_margin;
    double x1 = canvas_w - right_margin;
    double y1 = canvas_h - bottom_margin;

    if (!(x1 > x0) || !(y1 > y0)) {
        LOGE ("Некоректні поля — робоча область відсутня");
        return 2;
    }

    canvas_options_t canvas_opts = {
        .paper_w_mm = effective_w,
        .paper_h_mm = effective_h,
        .margin_top_mm = top_margin,
        .margin_right_mm = right_margin,
        .margin_bottom_mm = bottom_margin,
        .margin_left_mm = left_margin,
        .orientation = orient,
        .font_family = font_family,
    };

    canvas_plan_t plan;
    if (!canvas_plan_document (in.chars, in.len, &canvas_opts, &plan)) {
        LOGE ("canvas ще не реалізовано — побудова перервана");
        return 1;
    }

    canvas_plan_dispose (&plan);
    (void)dry_run;
    (void)verbose;
    LOGI ("Планування виконано (результат поки не використовується)");
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
    trace_write (
        LOG_INFO, "cmd.preview: bytes=%zu font=%s формат=%s орієнтація=%d", in.len,
        font_family ? font_family : "<типовий>", format == PREVIEW_FMT_PNG ? "png" : "svg",
        orientation);
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
 * Згенерувати прев’ю розкладки та повернути байти у буфер out.
 *
 * Контракт:
 * - `in` містить вхідний текст (прочитаний із файлу або переданий напряму).
 * - out->bytes/out->len встановлюються лише у разі успіху; викликач відповідає за free().
 * - Взаємодія з пристроєм не здійснюється.
 *
 * @param in               Текст для побудови прев’ю.
 * @param font_family      Родина шрифтів або NULL.
 * @param paper_w_mm       Ширина паперу (мм).
 * @param paper_h_mm       Висота паперу (мм).
 * @param margin_top_mm    Верхнє поле (мм).
 * @param margin_right_mm  Праве поле (мм).
 * @param margin_bottom_mm Нижнє поле (мм).
 * @param margin_left_mm   Ліве поле (мм).
 * @param orientation      Значення з enum orientation_t (див. args.h).
 * @param format           Формат прев’ю (SVG або PNG).
 * @param verbose          Рівень деталізації логів.
 * @param out              Буфер результату (байти виділяються всередині).
 * @return 0 успіх; ненульовий код — помилка.
 */

/**
 * Показати версію програми.
 *
 * @param verbose Рівень деталізації логів.
 * @return 0 успіх.
 */
cmd_result_t cmd_version_execute (verbose_level_t verbose) {
    if (verbose)
        LOGI ("Докладний режим виводу");
    trace_write (LOG_INFO, "cmd.version: виклик (verbose=%d)", verbose);
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
    trace_write (
        LOG_INFO,
        "cmd.device: kind=%d pen=%d motor=%d port=%s модель=%s jog=(%.3f,%.3f) verbose=%d",
        action ? action->kind : 0, action ? action->pen : 0, action ? action->motor : 0,
        port ? port : "<авто>", model ? model : "<типова>", jog_dx_mm, jog_dy_mm, verbose);
    if (!action || action->kind == DEVICE_ACTION_NONE) {
        LOGW ("Не вказано дію для пристрою");
        return 2;
    }

    switch (action->kind) {
    case DEVICE_ACTION_LIST:
        return cmd_device_list (model, verbose);
    case DEVICE_ACTION_SHELL:
        return cmd_device_shell (port, model, verbose);
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
    case DEVICE_ACTION_ABORT:
        return cmd_device_abort (port, model, verbose);
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
    trace_write (LOG_INFO, "cmd.sysinfo: запит (verbose=%d)", verbose);
    fprintf (stdout, "Системна інформація: ще не реалізовано\n");
    return 0;
}

/* ---- Реалізації деталізованих функцій (заглушки з логами) ---- */

/// Вивести перелік доступних векторних шрифтів (деталізована функція).
cmd_result_t cmd_font_list_execute (verbose_level_t verbose) {
    (void)verbose;
    LOGI ("Перелік доступних шрифтів");
    trace_write (LOG_INFO, "cmd.fonts: перелік шрифтів (verbose=%d)", verbose);
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
    trace_write (LOG_INFO, "cmd.config: show (verbose=%d)", verbose);
    fprintf (stdout, "Налаштування (показ): ще не реалізовано\n");
    return 0;
}

/// Скинути конфігурацію до типових значень (config --reset).
cmd_result_t cmd_config_reset (config_t *inout_cfg, verbose_level_t verbose) {
    (void)inout_cfg;
    (void)verbose;
    LOGI ("Скидання налаштувань");
    trace_write (LOG_INFO, "cmd.config: reset (verbose=%d)", verbose);
    fprintf (stdout, "Налаштування (скидання): ще не реалізовано\n");
    return 0;
}

/// Встановити значення конфігурації за парами key=value (config --set).
cmd_result_t cmd_config_set (const char *set_pairs, config_t *inout_cfg, verbose_level_t verbose) {
    (void)inout_cfg;
    (void)verbose;
    (void)set_pairs;
    LOGI ("Застосування нових налаштувань");
    trace_write (
        LOG_INFO, "cmd.config: set (%s)", set_pairs && *set_pairs ? set_pairs : "<порожньо>");
    fprintf (stdout, "Налаштування (встановлення): ще не реалізовано\n");
    return 0;
}

/**
 * @brief Опис потенційного серійного порту AxiDraw для device --list.
 */
typedef struct {
    char path[PATH_MAX]; /**< Повний шлях до порту. */
    bool responsive;     /**< true, якщо контролер відповів на запит V. */
    char version[64];    /**< Рядок версії прошивки, якщо доступний. */
    char detail[128];    /**< Пояснення у разі помилки. */
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
    ++(*count);
    return true;
}

/// Перелічити доступні порти пристроїв (device --list).
cmd_result_t cmd_device_list (const char *model, verbose_level_t verbose) {
    (void)model;
    (void)verbose;
    LOGI ("Перелік доступних портів");
    trace_write (
        LOG_INFO, "cmd.device.list: модель=%s verbose=%d", model ? model : "<типова>", verbose);

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

    if (count == 0) {
        fprintf (
            stdout, "Потенційних портів AxiDraw не знайдено. Підключіть пристрій і повторіть.\n");
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
            strncpy (
                ports[i].detail, "немає відповіді від контролера", sizeof (ports[i].detail) - 1);
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
    trace_write (
        LOG_INFO, "cmd.device.pen_up: порт=%s модель=%s verbose=%d", port ? port : "<авто>",
        model ? model : "<типова>", verbose);
    return with_axidraw_device (port, model, verbose, "підйом пера", device_pen_up_cb, NULL, true);
}

/// Опустити перо (device down).
cmd_result_t cmd_device_pen_down (const char *port, const char *model, verbose_level_t verbose) {
    LOGI ("Опускання пера");
    trace_write (
        LOG_INFO, "cmd.device.pen_down: порт=%s модель=%s verbose=%d", port ? port : "<авто>",
        model ? model : "<типова>", verbose);
    return with_axidraw_device (
        port, model, verbose, "опускання пера", device_pen_down_cb, NULL, true);
}

/// Перемкнути стан пера (device toggle).
cmd_result_t cmd_device_pen_toggle (const char *port, const char *model, verbose_level_t verbose) {
    LOGI ("Перемикання пера");
    trace_write (
        LOG_INFO, "cmd.device.pen_toggle: порт=%s модель=%s verbose=%d", port ? port : "<авто>",
        model ? model : "<типова>", verbose);
    return with_axidraw_device (
        port, model, verbose, "перемикання пера", device_pen_toggle_cb, NULL, true);
}

/// Увімкнути мотори (device motors-on).
cmd_result_t cmd_device_motors_on (const char *port, const char *model, verbose_level_t verbose) {
    LOGI ("Увімкнення моторів");
    trace_write (
        LOG_INFO, "cmd.device.motors_on: порт=%s модель=%s verbose=%d", port ? port : "<авто>",
        model ? model : "<типова>", verbose);
    return with_axidraw_device (
        port, model, verbose, "увімкнення моторів", device_motors_on_cb, NULL, false);
}

/// Вимкнути мотори (device motors-off).
cmd_result_t cmd_device_motors_off (const char *port, const char *model, verbose_level_t verbose) {
    LOGI ("Вимкнення моторів");
    trace_write (
        LOG_INFO, "cmd.device.motors_off: порт=%s модель=%s verbose=%d", port ? port : "<авто>",
        model ? model : "<типова>", verbose);
    return with_axidraw_device (
        port, model, verbose, "вимкнення моторів", device_motors_off_cb, NULL, false);
}

/// Аварійна зупинка (device abort).
cmd_result_t cmd_device_abort (const char *port, const char *model, verbose_level_t verbose) {
    LOGI ("Аварійна зупинка");
    trace_write (
        LOG_INFO, "cmd.device.abort: порт=%s модель=%s verbose=%d", port ? port : "<авто>",
        model ? model : "<типова>", verbose);
    return with_axidraw_device (
        port, model, verbose, "аварійна зупинка", device_abort_cb, NULL, false);
}

/// Перемістити у початкову позицію (home).
cmd_result_t cmd_device_home (const char *port, const char *model, verbose_level_t verbose) {
    LOGI ("Повернення у початкову позицію");
    trace_write (
        LOG_INFO, "cmd.device.home: порт=%s модель=%s verbose=%d", port ? port : "<авто>",
        model ? model : "<типова>", verbose);
    return with_axidraw_device (
        port, model, verbose, "повернення у домашню позицію", device_home_cb, NULL, false);
}

/// Ручний зсув на dx/dy у мм (jog).
cmd_result_t cmd_device_jog (
    const char *port, const char *model, double dx_mm, double dy_mm, verbose_level_t verbose) {
    LOGI ("Ручний зсув: по іксу %.3f мм, по ігреку %.3f мм", dx_mm, dy_mm);
    trace_write (
        LOG_INFO, "cmd.device.jog: порт=%s модель=%s dx=%.3f dy=%.3f verbose=%d",
        port ? port : "<авто>", model ? model : "<типова>", dx_mm, dy_mm, verbose);
    struct jog_ctx ctx = { .dx_mm = dx_mm, .dy_mm = dy_mm };
    return with_axidraw_device (port, model, verbose, "ручний зсув", device_jog_cb, &ctx, false);
}

/// Показати версію контролера (device version).
cmd_result_t cmd_device_version (const char *port, const char *model, verbose_level_t verbose) {
    LOGI ("Версія контролера");
    trace_write (
        LOG_INFO, "cmd.device.version: порт=%s модель=%s verbose=%d", port ? port : "<авто>",
        model ? model : "<типова>", verbose);
    return with_axidraw_device (
        port, model, verbose, "версія контролера", device_version_cb, NULL, false);
}

/// Показати статус пристрою (device status).
cmd_result_t cmd_device_status (const char *port, const char *model, verbose_level_t verbose) {
    LOGI ("Стан пристрою");
    trace_write (
        LOG_INFO, "cmd.device.status: порт=%s модель=%s verbose=%d", port ? port : "<авто>",
        model ? model : "<типова>", verbose);
    return with_axidraw_device (
        port, model, verbose, "статус пристрою", device_status_cb, NULL, false);
}

/// Показати поточну позицію (device position).
cmd_result_t cmd_device_position (const char *port, const char *model, verbose_level_t verbose) {
    LOGI ("Поточна позиція");
    trace_write (
        LOG_INFO, "cmd.device.position: порт=%s модель=%s verbose=%d", port ? port : "<авто>",
        model ? model : "<типова>", verbose);
    return with_axidraw_device (
        port, model, verbose, "поточна позиція", device_position_cb, NULL, false);
}

/// Скинути контролер (device reset).
cmd_result_t cmd_device_reset (const char *port, const char *model, verbose_level_t verbose) {
    LOGI ("Скидання контролера");
    trace_write (
        LOG_INFO, "cmd.device.reset: порт=%s модель=%s verbose=%d", port ? port : "<авто>",
        model ? model : "<типова>", verbose);
    return with_axidraw_device (
        port, model, verbose, "скидання стану", device_reset_cb, NULL, false);
}

/// Перезавантажити контролер (device reboot).
cmd_result_t cmd_device_reboot (const char *port, const char *model, verbose_level_t verbose) {
    LOGI ("Перезавантаження контролера");
    trace_write (
        LOG_INFO, "cmd.device.reboot: порт=%s модель=%s verbose=%d", port ? port : "<авто>",
        model ? model : "<типова>", verbose);
    return with_axidraw_device (
        port, model, verbose, "перезавантаження контролера", device_reboot_cb, NULL, false);
}
