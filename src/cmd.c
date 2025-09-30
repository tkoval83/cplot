#define _GNU_SOURCE
/**
 * @file cmd.c
 * @ingroup cmd
 * @brief Фасади CLI-підкоманд: `print`, `device`, `config`, `fonts`, `version`.
 *
 * @details
 * Цей модуль реалізує високорівневі обробники підкоманд CLI та координує роботу
 * між підсистемами: розкладка/рендеринг (drawing, svg/png), конфігурація (config),
 * шрифти (fontreg), а також взаємодія з обладнанням AxiDraw (axidraw/ebb/serial).
 *
 * Основні принципи:
 * - Вивід у stdout містить лише результати команд (наприклад, переліки, превʼю SVG/PNG).
 *   Журнали і попередження надсилаються через підсистему логування (log.c).
 * - Потік виводу результатів керується через `cmd_set_output()` і є потоково-локальним
 *   (`__thread`-змінна), що спрощує тестування та можливий паралелізм.
 * - Операції з пристроєм виконуються через шаблон `with_axidraw_device()`, який:
 *   захоплює lock-файл для ексклюзивного доступу, відкриває зʼєднання, викликає дію,
 *   за потреби очікує завершення рухів (FIFO/таймінги), та коректно відʼєднує пристрій.
 * - Превʼю формується в памʼяті (`SVG` або `PNG`) і повертається як байтовий буфер.
 * - Усі повідомлення та довідка локалізовані українською мовою.
 *
 * Коди повернення узгоджені з CLI: `0` — успіх, `1` — помилка виконання або валідації,
 * `2` — некоректне використання/відсутня дія.
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

#include "canvas.h"
#include "plot.h"
#include "serial.h"
#include "stepper.h"
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

/** Глобальний (thread-local) потік для результатів команд. */
static __thread FILE *g_cmd_out = NULL;

/**
 * @brief Формат превʼю, що генерується у памʼять.
 */
typedef enum {
    PREVIEW_FMT_SVG = 0, /**< Векторний SVG. */
    PREVIEW_FMT_PNG = 1, /**< Растровий PNG. */
} preview_fmt_t;
/**
 * @brief Серіалізація побудованої розкладки у SVG/PNG як байти.
 * @param layout Готова розкладка.
 * @param fmt Формат виводу (SVG або PNG).
 * @param out_bytes [out] Буфер результату (mallocʼується всередині; викликальник звільняє).
 * @param out_len [out] Розмір буфера.
 * @return 0 — успіх, інакше код помилки.
 */
static int cmd_layout_to_bytes (
    const drawing_layout_t *layout, preview_fmt_t fmt, uint8_t **out_bytes, size_t *out_len);

/**
 * @brief Рівень докладності для внутрішніх операцій команд.
 */
typedef enum {
    VERBOSE_OFF = 0,
    /**< Без додаткових журналів. */ VERBOSE_ON = 1 /**< Докладно. */
} verbose_level_t;

/**
 * @brief Потік виводу для результатів команд.
 * @return Потік (встановлений через cmd_set_output або stdout).
 */
static FILE *cmd_output_stream (void) { return g_cmd_out ? g_cmd_out : stdout; }

/**
 * @brief Встановлює користувацький потік виводу результатів.
 * @param out Потік або NULL для stdout.
 */
void cmd_set_output (FILE *out) { g_cmd_out = out; }

/** Зручний макрос для потоку виводу результатів команд. */
#define CMD_OUT cmd_output_stream ()

/**
 * @brief Переносить параметри з конфігурації до структур налаштувань AxiDraw.
 * @param out [out] Призначення налаштувань.
 * @param cfg Джерело конфігурації (може бути NULL — тоді лише скидання).
 */
static void cmd_axidraw_settings_from_config (axidraw_settings_t *out, const config_t *cfg) {
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
 * @brief Завантажує налаштування для моделі AxiDraw із профілю та дефолтів.
 * @param model Ідентифікатор моделі (NULL — типова).
 * @param settings [out] Налаштування для застосунку.
 * @return true — успіх, false — помилка.
 */
static bool cmd_load_axidraw_settings (const char *model, axidraw_settings_t *settings) {
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
    cmd_axidraw_settings_from_config (settings, &cfg);

    if (profile && profile->steps_per_mm > 0.0) {
        settings->steps_per_mm = profile->steps_per_mm;
    } else if (!(settings->steps_per_mm > 0.0)) {
        const axidraw_device_profile_t *defp = axidraw_device_profile_default ();
        if (defp && defp->steps_per_mm > 0.0)
            settings->steps_per_mm = defp->steps_per_mm;
    }

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

/** Максимальна тривалість сегмента руху у мілісекундах (обмеження EBB). */

/**
 * @brief Попереджає про зайнятість пристрою, читаючи інформацію з lock-файлу.
 */
static void cmd_warn_device_busy (void) {
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

/**
 * @brief Обчислює габарити робочої рамки залежно від орієнтації та полів.
 * @param page Параметри сторінки.
 * @param out_w [out] Ширина рамки.
 * @param out_h [out] Висота рамки.
 */
static void cmd_page_frame_dims (const drawing_page_t *page, double *out_w, double *out_h) {
    double fw = 0.0, fh = 0.0;
    if (page->orientation == ORIENT_PORTRAIT) {
        fw = page->paper_h_mm - page->margin_top_mm - page->margin_bottom_mm;
        fh = page->paper_w_mm - page->margin_left_mm - page->margin_right_mm;
    } else {
        fw = page->paper_w_mm - page->margin_left_mm - page->margin_right_mm;
        fh = page->paper_h_mm - page->margin_top_mm - page->margin_bottom_mm;
    }
    if (out_w)
        *out_w = fw;
    if (out_h)
        *out_h = fh;
}

/**
 * @brief Обмежує масштабу в діапазоні (0,1] та застосовує невеликий відступ.
 * @param s Вхідний коефіцієнт.
 * @return Обмежене значення.
 */
static double cmd_clamp_scale (double s) {
    if (!(s > 0.0))
        return 1.0;
    if (s > 1.0)
        s = 1.0;
    return s * 0.985;
}

/**
 * @brief Виводить параметри сторінки/полів на основі моделі та перевіряє коректність.
 * @param page [out] Параметри сторінки для заповнення.
 * @param device_model Ідентифікатор моделі пристрою (NULL — типова).
 * @param paper_w_mm Ширина паперу, мм (<=0 — взяти з профілю).
 * @param paper_h_mm Висота паперу, мм (<=0 — взяти з профілю).
 * @param margin_top_mm Верхнє поле, мм (<0 — взяти з профілю).
 * @param margin_right_mm Праве поле, мм (<0 — взяти з профілю).
 * @param margin_bottom_mm Нижнє поле, мм (<0 — взяти з профілю).
 * @param margin_left_mm Ліве поле, мм (<0 — взяти з профілю).
 * @param orientation Орієнтація (ORIENT_PORTRAIT/ORIENT_LANDSCAPE або інше — взяти з профілю).
 * @return 0 — успіх, 1/2 — помилка параметрів.
 */
static int cmd_build_print_page (
    drawing_page_t *page,
    const char *device_model,
    double paper_w_mm,
    double paper_h_mm,
    double margin_top_mm,
    double margin_right_mm,
    double margin_bottom_mm,
    double margin_left_mm,
    int orientation);


/**
 * @brief Тип колбека дії над підключеним пристроєм AxiDraw.
 * @param dev Підключений пристрій.
 * @param ctx Контекст користувача.
 * @return 0 — успіх, інакше помилка.
 */
typedef int (*device_cb_t) (axidraw_device_t *dev, void *ctx);

/**
 * @brief Відкриває пристрій, виконує колбек дії, чекає (опційно) і закриває пристрій.
 * @param port Шлях до порту (вже розвʼязаний).
 * @param model Модель профілю.
 * @param verbose Рівень докладності.
 * @param action_name Назва дії для журналів.
 * @param cb Колбек, що отримує dev.
 * @param ctx Контекст для колбека.
 * @param wait_idle Чекати завершення рухів після дії.
 * @return 0 — успіх, 1 — помилка/зайнятий пристрій.
 */
static int cmd_with_axidraw_device (
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
        cmd_warn_device_busy ();
        return 1;
    }

    axidraw_settings_t settings;
    if (!cmd_load_axidraw_settings (model, &settings)) {
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

    int rc = 0;
    if (cb)
        rc = cb (&dev, ctx);
    if (rc != 0)
        LOGE ("Дія '%s' завершилася з помилкою", action_name);

    int idle_rc = 0;
    if (rc == 0 && wait_idle)
        idle_rc = axidraw_wait_for_idle (&dev, 200);

    (void)action_name;

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
 * @brief Колбек: підняти перо.
 * @param dev Підключений пристрій.
 * @param ctx Не використовується (NULL).
 * @return 0 — успіх, -1 — помилка.
 */
static int cmd_device_pen_up_cb (axidraw_device_t *dev, void *ctx) {
    (void)ctx;
    int rc = axidraw_pen_up (dev);
    if (rc == 0)
        fprintf (CMD_OUT, "Перо піднято\n");
    return rc;
}

/**
 * @brief Колбек: опустити перо.
 * @param dev Підключений пристрій.
 * @param ctx Не використовується (NULL).
 * @return 0 — успіх, -1 — помилка.
 */
static int cmd_device_pen_down_cb (axidraw_device_t *dev, void *ctx) {
    (void)ctx;
    int rc = axidraw_pen_down (dev);
    if (rc == 0)
        fprintf (CMD_OUT, "Перо опущено\n");
    return rc;
}

/**
 * @brief Колбек: перемкнути стан пера (up/down).
 * @param dev Підключений пристрій.
 * @param ctx Не використовується (NULL).
 * @return 0 — успіх, -1 — помилка.
 */
static int cmd_device_pen_toggle_cb (axidraw_device_t *dev, void *ctx) {
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
 * @brief Колбек: увімкнути мотори.
 * @param dev Підключений пристрій.
 * @param ctx Не використовується (NULL).
 * @return 0 — успіх, -1 — помилка.
 */
static int cmd_device_motors_on_cb (axidraw_device_t *dev, void *ctx) {
    (void)ctx;
    int rc = ebb_enable_motors (dev->port, EBB_MOTOR_STEP_16, EBB_MOTOR_STEP_16, dev->timeout_ms);
    if (rc == 0)
        fprintf (CMD_OUT, "Мотори увімкнено (1/16 мікрокрок)\n");
    return rc;
}

/**
 * @brief Колбек: вимкнути мотори.
 * @param dev Підключений пристрій.
 * @param ctx Не використовується (NULL).
 * @return 0 — успіх, -1 — помилка.
 */
static int cmd_device_motors_off_cb (axidraw_device_t *dev, void *ctx) {
    (void)ctx;
    int rc = ebb_disable_motors (dev->port, dev->timeout_ms);
    if (rc == 0)
        fprintf (CMD_OUT, "Мотори вимкнено\n");
    return rc;
}

/**
 * @brief Колбек: аварійна зупинка.
 * @param dev Підключений пристрій.
 * @param ctx Не використовується (NULL).
 * @return 0 — успіх, -1 — помилка.
 */
static int cmd_device_abort_cb (axidraw_device_t *dev, void *ctx) {
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
 * @brief Колбек: ручний зсув на dx/dy мм.
 * @param dev Підключений пристрій.
 * @param ctx Контекст зі зсувами (jog_ctx_t*).
 * @return 0 — успіх, -1 — помилка.
 */
static int cmd_device_jog_cb (axidraw_device_t *dev, void *ctx) {
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

    const axidraw_settings_t *cfg = axidraw_device_settings (dev);
    double speed = (cfg && cfg->speed_mm_s > 0.0) ? cfg->speed_mm_s : 75.0;
    rc = axidraw_move_mm (dev, dx, dy, speed);
    if (rc == 0 && axidraw_wait_for_idle (dev, 200) == 0) {
        int32_t s1 = 0, s2 = 0;
        if (ebb_query_steps (dev->port, &s1, &s2, dev->timeout_ms) == 0) {
            double spmm = (cfg && cfg->steps_per_mm > 0.0) ? cfg->steps_per_mm : 0.0;
            if (spmm > 0.0) {
                double pos_x = s1 / spmm;
                double pos_y = s2 / spmm;
                fprintf (
                    CMD_OUT, "Зсув виконано. Поточна позиція: X=%.3f мм, Y=%.3f мм\n", pos_x,
                    pos_y);
            } else {
                fprintf (
                    CMD_OUT, "Зсув виконано. Поточна позиція: %d (кроки осі 1), %d (кроки осі 2)\n",
                    s1, s2);
            }
        }
    }
    return rc;
}

/**
 * @brief Колбек: повернення у home.
 * @param dev Підключений пристрій.
 * @param ctx Не використовується (NULL).
 * @return 0 — успіх, -1 — помилка.
 */
static int cmd_device_home_cb (axidraw_device_t *dev, void *ctx) {
    (void)ctx;
    int rc = axidraw_home_default (dev);
    if (rc == 0)
        fprintf (CMD_OUT, "Домашнє позиціювання завершено\n");
    return rc;
}

/**
 * @brief Колбек: друк версії контролера.
 * @param dev Підключений пристрій.
 * @param ctx Не використовується (NULL).
 * @return 0 — успіх, -1 — помилка.
 */
static int cmd_device_version_cb (axidraw_device_t *dev, void *ctx) {
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
 * @brief Колбек: зведений статус контролера.
 * @param dev Підключений пристрій.
 * @param ctx Не використовується (NULL).
 * @return 0 — успіх, -1 — помилка.
 */
static int cmd_device_status_cb (axidraw_device_t *dev, void *ctx) {
    (void)ctx;
    ebb_motion_status_t ms = { 0 };
    if (ebb_query_motion (dev->port, &ms, dev->timeout_ms) != 0)
        return -1;
    int32_t s1 = 0, s2 = 0;
    (void)ebb_query_steps (dev->port, &s1, &s2, dev->timeout_ms);
    bool pen_up = false;
    (void)ebb_query_pen (dev->port, &pen_up, dev->timeout_ms);
    bool servo_on = false;
    (void)ebb_query_servo_power (dev->port, &servo_on, dev->timeout_ms);
    char version[64] = { 0 };
    (void)ebb_query_version (dev->port, version, sizeof (version), dev->timeout_ms);

    fprintf (CMD_OUT, "Статус пристрою:\n");
    fprintf (CMD_OUT, "  Команда активна: %s\n", ms.command_active ? "так" : "ні");
    fprintf (CMD_OUT, "  Мотор X активний: %s\n", ms.motor1_active ? "так" : "ні");
    fprintf (CMD_OUT, "  Мотор Y активний: %s\n", ms.motor2_active ? "так" : "ні");
    fprintf (CMD_OUT, "  FIFO непорожній: %s\n", ms.fifo_pending ? "так" : "ні");
    double spmm = 0.0;
    const axidraw_settings_t *cfg = axidraw_device_settings (dev);
    if (cfg && cfg->steps_per_mm > 0.0)
        spmm = cfg->steps_per_mm;
    if (spmm > 0.0) {
        fprintf (CMD_OUT, "  Позиція X: %.3f мм\n", s1 / spmm);
        fprintf (CMD_OUT, "  Позиція Y: %.3f мм\n", s2 / spmm);
    } else {
        fprintf (CMD_OUT, "  Позиція X: (невідомо — steps_per_mm не встановлено)\n");
        fprintf (CMD_OUT, "  Позиція Y: (невідомо — steps_per_mm не встановлено)\n");
    }
    fprintf (CMD_OUT, "  Перо підняте: %s\n", pen_up ? "так" : "ні");
    fprintf (CMD_OUT, "  Серво увімкнено: %s\n", servo_on ? "так" : "ні");
    if (version[0])
        fprintf (CMD_OUT, "  Прошивка: %s\n", version);
    return 0;
}

/**
 * @brief Колбек: друк позиції у кроках/мм.
 * @param dev Підключений пристрій.
 * @param ctx Не використовується (NULL).
 * @return 0 — успіх, -1 — помилка.
 */
static int cmd_device_position_cb (axidraw_device_t *dev, void *ctx) {
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
 * @brief Колбек: скинути стан (pen up, clear steps, motors off).
 * @param dev Підключений пристрій.
 * @param ctx Не використовується (NULL).
 * @return 0 — успіх, -1 — помилка.
 */
static int cmd_device_reset_cb (axidraw_device_t *dev, void *ctx) {
    (void)ctx;
    int rc = axidraw_pen_up (dev);
    if (rc != 0)
        return rc;
    if (axidraw_wait_for_idle (dev, 200) != 0)
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
 * @brief Колбек: перезавантажити контролер.
 * @param dev Підключений пристрій.
 * @param ctx Не використовується (NULL).
 * @return 0 — успіх, -1 — помилка.
 */
static int cmd_device_reboot_cb (axidraw_device_t *dev, void *ctx) {
    (void)ctx;
    if (serial_write_line (dev->port, "RB") != 0) {
        LOGE ("Не вдалося надіслати команду перезавантаження");
        return -1;
    }
    fprintf (CMD_OUT, "Перезавантаження контролера ініційовано\n");
    return 0;
}

#ifndef PATH_MAX
/** Fallback визначення PATH_MAX для платформ без нього. */
#define PATH_MAX 4096
#endif

/**
 * @brief Опис одного знайденого серійного порту.
 */
typedef struct {
    char path[PATH_MAX]; /**< Повний шлях до tty-порту. */
    bool responsive;     /**< true, якщо контролер відповідає на запити. */
    char version[64];    /**< Рядок версії прошивки (якщо відома). */
    char detail[128];    /**< Додаткові подробиці (помилка/статус). */
    char alias[64];      /**< Зручний псевдонім, похідний від імені порту. */
} device_port_info_t;

/**
 * @brief Узагальнена інформація про обраний пристрій та профіль руху.
 */
typedef struct {
    double paper_w_mm;        /**< Робоча ширина, мм. */
    double paper_h_mm;        /**< Робоча висота, мм. */
    double speed_mm_s;        /**< Рекомендована швидкість, мм/с. */
    double accel_mm_s2;       /**< Рекомендоване прискорення, мм/с^2. */
    char alias[64];           /**< Обраний псевдонім. */
    char port[PATH_MAX];      /**< Шлях до порту. */
    char profile_model[64];   /**< Назва застосованої моделі профілю. */
    char requested_model[64]; /**< Запитана користувачем модель. */
    bool auto_selected;       /**< true, якщо порт обрано автоматично. */
} device_profile_info_t;

/**
 * @brief Гарантує, що у конфігурації встановлено робоче поле та динаміку з профілю пристрою.
 * @param cfg [in,out] Конфігурація, що доповнюється.
 * @param inout_alias [in,out] Псевдонім пристрою; може бути оновлений.
 * @param alias_len Розмір буфера псевдоніма.
 * @param model Бажана модель профілю (NULL — типова).
 * @param verbose Рівень докладності.
 * @return true — успіх, false — не вдалося визначити профіль/порт.
 */
static bool cmd_ensure_device_profile (
    config_t *cfg, char *inout_alias, size_t alias_len, const char *model, verbose_level_t verbose);

/**
 * @brief Формує псевдонім пристрою з базового імені шляху.
 * @param path Шлях (наприклад, /dev/ttyACM0).
 * @param out [out] Буфер для псевдоніма.
 * @param out_len Розмір буфера.
 */
static void cmd_derive_port_alias (const char *path, char *out, size_t out_len) {
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

/** Перевіряє, чи порт уже є у списку. */
static bool cmd_device_port_exists (const device_port_info_t *ports, size_t count, const char *path) {
    if (!ports || !path)
        return false;
    for (size_t i = 0; i < count; ++i) {
        if (strcmp (ports[i].path, path) == 0)
            return true;
    }
    return false;
}

/**
 * @brief Додає порт у динамічний масив, розширюючи за потреби.
 * @param ports [in,out] Вказівник на масив портів (realloc усередині).
 * @param count [in,out] Кількість елементів (збільшується при успіху).
 * @param capacity [in,out] Ємність масиву (може зрости).
 * @param path Шлях до tty-порту для додавання.
 * @return true — успіх, false — помилка виділення памʼяті.
 */
static bool
cmd_device_port_add (device_port_info_t **ports, size_t *count, size_t *capacity, const char *path) {
    if (!ports || !count || !capacity || !path || !*path)
        return false;
    if (*ports && cmd_device_port_exists (*ports, *count, path))
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
    cmd_derive_port_alias (slot->path, slot->alias, sizeof (slot->alias));
    ++(*count);
    return true;
}

/**
 * @brief Збирає список потенційних портів AxiDraw із кількох шаблонів.
 * @param ports [out] Динамічний масив портів.
 * @param count [out] Кількість.
 * @param capacity [in,out] Ємність масиву.
 * @return 0 — успіх, -1 — помилка виділення.
 */
static int cmd_collect_device_ports (device_port_info_t **ports, size_t *count, size_t *capacity) {
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
                if (!cmd_device_port_add (ports, count, capacity, g.gl_pathv[j])) {
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
        if (!cmd_device_port_add (ports, count, capacity, guessed)) {
            LOGE ("Недостатньо пам'яті для переліку портів");
            return -1;
        }
    }
#endif

    return 0;
}

/**
 * @brief Перевіряє відповіді від контролера для кожного порту та зчитує версію.
 */
static void cmd_probe_device_ports (device_port_info_t *ports, size_t count) {
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
 * @brief Визначає придатний порт на основі псевдоніма або авто-вибору.
 * @param alias Бажаний псевдонім/шлях (може бути NULL).
 * @param out_path [out] Обраний шлях порту.
 * @param out_len Розмір буфера шляху.
 * @param out_alias [out] Обраний псевдонім (опційно).
 * @param out_alias_len Розмір буфера псевдоніма.
 * @param out_auto_selected [out] true, якщо порт обрано автоматично.
 * @return 0 — успіх, 1 — не знайдено жодного, 2 — псевдонім не знайдено, -1 — помилка.
 */
static int cmd_resolve_device_port (
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
    if (cmd_collect_device_ports (&ports, &count, &capacity) != 0) {
        free (ports);
        return -1;
    }
    if (count == 0) {
        free (ports);
        return 1;
    }

    cmd_probe_device_ports (ports, count);

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
 * @brief Локальна реалізація `device list` — друк знайдених портів.
 */
static int cmd_device_list_local (const char *model, verbose_level_t verbose) {
    (void)model;
    (void)verbose;
    LOGI ("Перелік доступних портів");
    log_print (LOG_INFO, "device.list: модель=%s verbose=%d", model ? model : "<типова>", verbose);

    device_port_info_t *ports = NULL;
    size_t count = 0;
    size_t capacity = 0;

    if (cmd_collect_device_ports (&ports, &count, &capacity) != 0) {
        free (ports);
        return 1;
    }

    if (count == 0) {
        fprintf (
            CMD_OUT, "Потенційних портів AxiDraw не знайдено. Підключіть пристрій і повторіть.\n");
        free (ports);
        return 0;
    }

    cmd_probe_device_ports (ports, count);

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
 * @brief Зчитує профіль/порт/аліас для пристрою для друку користувачу.
 * @param alias Псевдонім або NULL.
 * @param model Модель профілю або NULL.
 * @param info [out] Заповнені поля.
 * @param verbose Режим докладності.
 * @return true — успіх, false — помилка.
 */
static bool cmd_device_profile_local (
    const char *alias, const char *model, device_profile_info_t *info, verbose_level_t verbose) {
    if (!info)
        return false;
    memset (info, 0, sizeof (*info));

    char port_buf[PATH_MAX];
    char resolved_alias[64];
    bool auto_selected = false;

    int resolve_rc = cmd_resolve_device_port (
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

#if 0
static __thread const char *g_device_alias_ctx = NULL;

static int cmd_run_device_list (
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
    return cmd_device_list_local (model, v);
}

static int cmd_run_device_profile (
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

    if (!cmd_device_profile_local (g_device_alias_ctx, model, &info, v))
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

static int cmd_run_device_pen (
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
        return cmd_with_axidraw_device (port, model, v, "підйом пера", cmd_device_pen_up_cb, NULL, true);
    case DEVICE_PEN_DOWN:
        return cmd_with_axidraw_device (
            port, model, v, "опускання пера", cmd_device_pen_down_cb, NULL, true);
    case DEVICE_PEN_TOGGLE:
        return cmd_with_axidraw_device (
            port, model, v, "перемикання пера", cmd_device_pen_toggle_cb, NULL, true);
    default:
        return 2;
    }
}

static int cmd_run_device_motors (
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
        return cmd_with_axidraw_device (
            port, model, v, "увімкнення моторів", cmd_device_motors_on_cb, NULL, false);
    case DEVICE_MOTOR_OFF:
        return cmd_with_axidraw_device (
            port, model, v, "вимкнення моторів", cmd_device_motors_off_cb, NULL, false);
    default:
        return 2;
    }
}

static int cmd_run_device_abort (
    const device_action_t *a,
    const char *port,
    const char *model,
    double dx,
    double dy,
    verbose_level_t v) {
    (void)a;
    (void)dx;
    (void)dy;
    return cmd_with_axidraw_device (port, model, v, "аварійна зупинка", cmd_device_abort_cb, NULL, false);
}

static int cmd_run_device_home (
    const device_action_t *a,
    const char *port,
    const char *model,
    double dx,
    double dy,
    verbose_level_t v) {
    (void)a;
    (void)dx;
    (void)dy;
    return cmd_with_axidraw_device (port, model, v, "home", cmd_device_home_cb, NULL, true);
}

static int cmd_run_device_jog (
    const device_action_t *a,
    const char *port,
    const char *model,
    double dx,
    double dy,
    verbose_level_t v) {
    (void)a;
    jog_ctx_t ctx = { .dx_mm = dx, .dy_mm = dy };
    return cmd_with_axidraw_device (port, model, v, "ручний зсув", cmd_device_jog_cb, &ctx, false);
}

static int cmd_run_device_version (
    const device_action_t *a,
    const char *port,
    const char *model,
    double dx,
    double dy,
    verbose_level_t v) {
    (void)a;
    (void)dx;
    (void)dy;
    return cmd_with_axidraw_device (port, model, v, "версія", cmd_device_version_cb, NULL, false);
}

static int cmd_run_device_status (
    const device_action_t *a,
    const char *port,
    const char *model,
    double dx,
    double dy,
    verbose_level_t v) {
    (void)a;
    (void)dx;
    (void)dy;
    return cmd_with_axidraw_device (port, model, v, "статус", cmd_device_status_cb, NULL, false);
}

static int cmd_run_device_position (
    const device_action_t *a,
    const char *port,
    const char *model,
    double dx,
    double dy,
    verbose_level_t v) {
    (void)a;
    (void)dx;
    (void)dy;
    return cmd_with_axidraw_device (port, model, v, "позиція", cmd_device_position_cb, NULL, false);
}

static int cmd_run_device_reset (
    const device_action_t *a,
    const char *port,
    const char *model,
    double dx,
    double dy,
    verbose_level_t v) {
    (void)a;
    (void)dx;
    (void)dy;
    return cmd_with_axidraw_device (port, model, v, "скидання", cmd_device_reset_cb, NULL, false);
}

static int cmd_run_device_reboot (
    const device_action_t *a,
    const char *port,
    const char *model,
    double dx,
    double dy,
    verbose_level_t v) {
    (void)a;
    (void)dx;
    (void)dy;
    return cmd_with_axidraw_device (port, model, v, "перезавантаження", cmd_device_reboot_cb, NULL, false);
}
#endif

#if 0
static int cmd_device_execute_local (
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
            = cmd_resolve_device_port (requested, port_buf, sizeof (port_buf), NULL, 0, NULL);
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
        { DEVICE_ACTION_LIST, cmd_run_device_list },
        { DEVICE_ACTION_PROFILE, cmd_run_device_profile },
        { DEVICE_ACTION_PEN, cmd_run_device_pen },
        { DEVICE_ACTION_MOTORS, cmd_run_device_motors },
        { DEVICE_ACTION_ABORT, cmd_run_device_abort },
        { DEVICE_ACTION_HOME, cmd_run_device_home },
        { DEVICE_ACTION_JOG, cmd_run_device_jog },
        { DEVICE_ACTION_VERSION, cmd_run_device_version },
        { DEVICE_ACTION_STATUS, cmd_run_device_status },
        { DEVICE_ACTION_POSITION, cmd_run_device_position },
        { DEVICE_ACTION_RESET, cmd_run_device_reset },
        { DEVICE_ACTION_REBOOT, cmd_run_device_reboot },
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

/** Розбір числа з плаваючою крапкою (strict). */
static bool cmd_parse_double_str (const char *value, double *out) {
    if (!value || !out)
        return false;
    char *endptr = NULL;
    double v = strtod (value, &endptr);
    if (endptr == value || (endptr && *endptr))
        return false;
    *out = v;
    return true;
}

/** Розбір десяткового цілого зі строгими перевірками. */
static bool cmd_parse_int_str (const char *value, int *out) {
    if (!value || !out)
        return false;
    char *endptr = NULL;
    long v = strtol (value, &endptr, 10);

    if (endptr == value || (endptr && *endptr))
        return false;

    if (v > INT_MAX || v < INT_MIN)
        return false;
    *out = (int)v;
    return true;
}

/**
 * @brief Застосовує одну пару key=value до конфігурації (обмежений перелік ключів).
 * @return 0 — успіх, -1 — невідомий ключ або некоректне значення.
 */
static int cmd_config_apply_pair (config_t *cfg, const char *key_raw, const char *value_raw) {
    if (!cfg || !key_raw || !value_raw)
        return -1;

    char key[64];
    strncpy (key, key_raw, sizeof (key) - 1);
    key[sizeof (key) - 1] = '\0';
    str_string_trim_ascii (key);
    str_string_to_lower_ascii (key);

    char value_buf[256];
    strncpy (value_buf, value_raw, sizeof (value_buf) - 1);
    value_buf[sizeof (value_buf) - 1] = '\0';
    str_string_trim_ascii (value_buf);

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

    if (strcmp (key, "paper_w_mm") == 0 || strcmp (key, "paper_w") == 0
        || strcmp (key, "width") == 0)
        return -1;
    if (strcmp (key, "paper_h_mm") == 0 || strcmp (key, "paper_h") == 0
        || strcmp (key, "height") == 0)
        return -1;
    if (strcmp (key, "margin_top_mm") == 0) {
        if (!cmd_parse_double_str (value_buf, &dbl))
            return -1;
        cfg->margin_top_mm = dbl;
        return 0;
    }
    if (strcmp (key, "margin_right_mm") == 0) {
        if (!cmd_parse_double_str (value_buf, &dbl))
            return -1;
        cfg->margin_right_mm = dbl;
        return 0;
    }
    if (strcmp (key, "margin_bottom_mm") == 0) {
        if (!cmd_parse_double_str (value_buf, &dbl))
            return -1;
        cfg->margin_bottom_mm = dbl;
        return 0;
    }
    if (strcmp (key, "margin_left_mm") == 0) {
        if (!cmd_parse_double_str (value_buf, &dbl))
            return -1;
        cfg->margin_left_mm = dbl;
        return 0;
    }
    if (strcmp (key, "font_size_pt") == 0 || strcmp (key, "font_size") == 0) {
        if (!cmd_parse_double_str (value_buf, &dbl))
            return -1;
        cfg->font_size_pt = dbl;
        return 0;
    }
    if (strcmp (key, "speed_mm_s") == 0 || strcmp (key, "speed") == 0) {
        if (!cmd_parse_double_str (value_buf, &dbl))
            return -1;
        cfg->speed_mm_s = dbl;
        return 0;
    }
    if (strcmp (key, "accel_mm_s2") == 0 || strcmp (key, "accel") == 0) {
        if (!cmd_parse_double_str (value_buf, &dbl))
            return -1;
        cfg->accel_mm_s2 = dbl;
        return 0;
    }

    if (strcmp (key, "pen_up_pos") == 0 || strcmp (key, "pen_up") == 0)
        return -1;
    if (strcmp (key, "pen_down_pos") == 0 || strcmp (key, "pen_down") == 0)
        return -1;
    if (strcmp (key, "pen_up_speed") == 0) {
        if (!cmd_parse_int_str (value_buf, &integer))
            return -1;
        cfg->pen_up_speed = integer;
        return 0;
    }
    if (strcmp (key, "pen_down_speed") == 0) {
        if (!cmd_parse_int_str (value_buf, &integer))
            return -1;
        cfg->pen_down_speed = integer;
        return 0;
    }
    if (strcmp (key, "pen_up_delay_ms") == 0 || strcmp (key, "pen_up_delay") == 0) {
        if (!cmd_parse_int_str (value_buf, &integer))
            return -1;
        cfg->pen_up_delay_ms = integer;
        return 0;
    }
    if (strcmp (key, "pen_down_delay_ms") == 0 || strcmp (key, "pen_down_delay") == 0) {
        if (!cmd_parse_int_str (value_buf, &integer))
            return -1;
        cfg->pen_down_delay_ms = integer;
        return 0;
    }
    if (strcmp (key, "servo_timeout_s") == 0 || strcmp (key, "servo_timeout") == 0) {
        if (!cmd_parse_int_str (value_buf, &integer))
            return -1;
        cfg->servo_timeout_s = integer;
        return 0;
    }
    if (strcmp (key, "orientation") == 0 || strcmp (key, "orient") == 0)
        return -1;

    if (strcmp (key, "margin") == 0 || strcmp (key, "margins") == 0) {
        if (!cmd_parse_double_str (value_buf, &dbl))
            return -1;
        cfg->margin_top_mm = cfg->margin_right_mm = cfg->margin_bottom_mm = cfg->margin_left_mm
            = dbl;
        return 0;
    }

    return -1;
}

/**
 * @brief Реалізація cmd_ensure_device_profile(): доповнення конфігурації з даних пристрою.
 * @param cfg [in,out] Конфігурація.
 * @param inout_alias [in,out] Псевдонім пристрою.
 * @param alias_len Розмір буфера псевдоніма.
 * @param model Бажана модель профілю (опційно).
 * @param verbose Рівень докладності.
 */
static bool cmd_ensure_device_profile (
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

    if (!cmd_device_profile_local (requested_alias, model, &info, verbose))
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

static int cmd_build_print_page (
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

static int cmd_layout_to_bytes (
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

/**
 * @brief Виконує побудову розкладки та друк (або симуляцію) без генерації превʼю.
 * @return 0 — успіх, інакше код помилки.
 */
/**
 * @brief Виконує побудову розкладки та друк (або симуляцію) без генерації превʼю.
 * @param in_chars Вхідний текст.
 * @param in_len Довжина вхідного тексту (байти).
 * @param markdown true — інтерпретувати як Markdown.
 * @param family Родина шрифтів (NULL — брати з конфігурації).
 * @param font_size Кегль у пунктах (<=0 — з конфігурації).
 * @param model Модель пристрою (NULL — типова).
 * @param paper_w Ширина паперу, мм (<=0 — з профілю).
 * @param paper_h Висота паперу, мм (<=0 — з профілю).
 * @param margin_top Верхнє поле, мм (<0 — з конфіг.).
 * @param margin_right Праве поле, мм (<0 — з конфіг.).
 * @param margin_bottom Нижнє поле, мм (<0 — з конфіг.).
 * @param margin_left Ліве поле, мм (<0 — з конфіг.).
 * @param orientation Орієнтація (портрет/альбом).
 * @param fit_page true — масштабувати під рамку.
 * @param dry_run true — без надсилання на пристрій.
 * @param verbose true — докладні журнали.
 * @return 0 — успіх, інакше код помилки.
 */
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
    bool fit_page,
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
    int setup_rc = cmd_build_print_page (
        &page, model, paper_w, paper_h, margin_top, margin_right, margin_bottom, margin_left,
        orientation);
    if (setup_rc != 0)
        return setup_rc;
    page.fit_to_frame = fit_page ? 1 : 0;
    LOGD ("cmd: fit_page flag=%d", page.fit_to_frame);

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

        if (page.fit_to_frame) {
            double fw, fh;
            cmd_page_frame_dims (&page, &fw, &fh);
            geom_bbox_t bb;
            if (geom_bbox_of_paths (&md_paths, &bb) == 0) {
                double cw = bb.max_x - bb.min_x;
                double ch = bb.max_y - bb.min_y;
                if ((cw > 0.0 && ch > 0.0) && ((cw > fw) || (ch > fh))) {
                    geom_paths_free (&md_paths);
                    double sx = fw / cw;
                    double sy = fh / ch;
                    double s = cmd_clamp_scale (sx < sy ? sx : sy);
                    double new_pt = mopts.base_size_pt * s;
                    if (new_pt < 3.0)
                        new_pt = 3.0;
                    mopts.base_size_pt = new_pt;

                    if (markdown_render_paths (input.chars, &mopts, &md_paths, NULL) != 0)
                        return 1;
                }
            }
        }
        drawing_layout_t layout_info;
        if (drawing_build_layout_from_paths (&page, &md_paths, &layout_info) != 0) {
            geom_paths_free (&md_paths);
            return 1;
        }
        geom_paths_free (&md_paths);

        int rc = plot_canvas_execute (&layout_info.layout, model, dry_run, verbose);
        drawing_layout_dispose (&layout_info);
        return rc;
    } else {
        drawing_layout_t layout_info = { 0 };
        if (drawing_build_layout (&page, family, font_size, input, &layout_info) != 0)
            return 1;

        if (page.fit_to_frame) {
            double fw, fh;
            cmd_page_frame_dims (&page, &fw, &fh);
            geom_bbox_t bb = layout_info.layout.bounds_mm;
            double cw = bb.max_x - bb.min_x;
            double ch = bb.max_y - bb.min_y;
            if ((cw > 0.0 && ch > 0.0) && ((cw > fw) || (ch > fh))) {
                drawing_layout_dispose (&layout_info);
                double sx = fw / cw;
                double sy = fh / ch;
                double s = cmd_clamp_scale (sx < sy ? sx : sy);
                double new_pt = font_size * s;
                if (new_pt < 3.0)
                    new_pt = 3.0;
                if (drawing_build_layout (&page, family, new_pt, input, &layout_info) != 0)
                    return 1;
            }
        }
        int rc = plot_canvas_execute (&layout_info.layout, model, dry_run, verbose);
        drawing_layout_dispose (&layout_info);
        return rc;
    }
}

/**
 * @brief Формує превʼю SVG/PNG для заданого вхідного тексту та параметрів сторінки.
 * @return 0 — успіх, інакше код помилки.
 */
/**
 * @brief Формує превʼю SVG/PNG для заданого вхідного тексту та параметрів сторінки.
 * @param in_chars Вхідний текст.
 * @param in_len Довжина вхідного тексту (байти).
 * @param markdown true — інтерпретувати як Markdown.
 * @param family Родина шрифтів (NULL — брати з конфігурації).
 * @param font_size Кегль у пунктах (<=0 — з конфігурації).
 * @param model Модель пристрою (NULL — типова).
 * @param paper_w Ширина паперу, мм (<=0 — з профілю).
 * @param paper_h Висота паперу, мм (<=0 — з профілю).
 * @param margin_top Верхнє поле, мм (<0 — з конфіг.).
 * @param margin_right Праве поле, мм (<0 — з конфіг.).
 * @param margin_bottom Нижнє поле, мм (<0 — з конфіг.).
 * @param margin_left Ліве поле, мм (<0 — з конфіг.).
 * @param orientation Орієнтація (портрет/альбом).
 * @param fit_page 1 — масштабувати під рамку.
 * @param preview_png 1 — PNG, 0 — SVG.
 * @param verbose true — докладні журнали.
 * @param out_bytes [out] Байти результату (malloc).
 * @param out_len [out] Довжина буфера.
 * @return 0 — успіх, інакше код помилки.
 */
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
    int fit_page,
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
    int setup_rc = cmd_build_print_page (
        &page, model, paper_w, paper_h, margin_top, margin_right, margin_bottom, margin_left,
        orientation);
    if (setup_rc != 0)
        return setup_rc;
    page.fit_to_frame = fit_page ? 1 : 0;

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
        if (page.fit_to_frame) {
            double fw, fh;
            cmd_page_frame_dims (&page, &fw, &fh);
            geom_bbox_t bb;
            if (geom_bbox_of_paths (&md_paths, &bb) == 0) {
                double cw = bb.max_x - bb.min_x;
                double ch = bb.max_y - bb.min_y;
                if ((cw > 0.0 && ch > 0.0) && ((cw > fw) || (ch > fh))) {
                    geom_paths_free (&md_paths);
                    double sx = fw / cw, sy = fh / ch;
                    double s = cmd_clamp_scale (sx < sy ? sx : sy);
                    double new_pt = mopts.base_size_pt * s;
                    if (new_pt < 3.0)
                        new_pt = 3.0;
                    mopts.base_size_pt = new_pt;
                    if (markdown_render_paths (input.chars, &mopts, &md_paths, NULL) != 0)
                        return 1;
                }
            }
        }
        drawing_layout_t layout_info;
        if (drawing_build_layout_from_paths (&page, &md_paths, &layout_info) != 0) {
            geom_paths_free (&md_paths);
            return 1;
        }
        geom_paths_free (&md_paths);
        rc = cmd_layout_to_bytes (&layout_info, format, out_bytes, out_len);
        drawing_layout_dispose (&layout_info);
    } else {
        drawing_layout_t layout_info = { 0 };
        if (drawing_build_layout (&page, family, font_size, input, &layout_info) != 0)
            return 1;
        if (page.fit_to_frame) {
            double fw, fh;
            cmd_page_frame_dims (&page, &fw, &fh);
            geom_bbox_t bb = layout_info.layout.bounds_mm;
            double cw = bb.max_x - bb.min_x;
            double ch = bb.max_y - bb.min_y;
            if ((cw > 0.0 && ch > 0.0) && ((cw > fw) || (ch > fh))) {
                drawing_layout_dispose (&layout_info);
                double sx = fw / cw, sy = fh / ch;
                double s = cmd_clamp_scale (sx < sy ? sx : sy);
                double new_pt = font_size * s;
                if (new_pt < 3.0)
                    new_pt = 3.0;
                if (drawing_build_layout (&page, family, new_pt, input, &layout_info) != 0)
                    return 1;
            }
        }
        rc = cmd_layout_to_bytes (&layout_info, format, out_bytes, out_len);
        drawing_layout_dispose (&layout_info);
    }
    return rc;
}

/**
 * @brief Друкує рядок версії програми у вихідний потік.
 * @param verbose true — друкувати додаткові журнали.
 * @return 0 — успіх.
 */
cmd_result_t cmd_version_execute (bool verbose) {
    if (verbose)
        LOGI ("Докладний режим виводу");
    fprintf (CMD_OUT, "%s %s — %s\n", __PROGRAM_NAME__, __PROGRAM_VERSION__, __PROGRAM_AUTHOR__);
    return 0;
}

/**
 * @brief Друк переліку шрифтів (еквівалент `cmd_font_list_execute(false)`).
 * @param verbose true — друкувати додаткові журнали.
 * @return 0 — успіх, інакше код помилки.
 */
cmd_result_t cmd_fonts_execute (bool verbose) { return cmd_font_list_execute (0, verbose); }

/**
 * @brief Виконує дію конфігурації: show/reset/set.
 * @param action 1=show, 2=reset, 3=set.
 * @param set_pairs Кома-розділений список key=value (для action=3).
 * @param inout_cfg Конфіг (NULL — авто-завантаження/збереження).
 * @param verbose true — додаткові журнали.
 * @return 0 — успіх, інакше код помилки.
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
 * @brief Друкує список шрифтів або родин шрифтів.
 * @param families_only Якщо 1 — лише родини; 0 — всі шрифти.
 * @param verbose true — докладні журнали.
 * @return 0 — успіх, інакше код помилки.
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
 * @brief Друк поточної конфігурації користувача.
 * @param cfg Конфіг (NULL — авто-завантаження).
 * @param verbose Докладні журнали.
 * @return 0 — успіх.
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
 * @brief Скидання конфігурації до дефолтів із збереженням.
 * @param inout_cfg Конфіг (NULL — авто-завантаження/збереження).
 * @param verbose Докладні журнали.
 * @return 0 — успіх, інакше помилка.
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

    if (!cmd_ensure_device_profile (inout_cfg, alias_buf, sizeof (alias_buf), NULL, verbose))
        return 1;

    if (config_save (inout_cfg) != 0) {
        fprintf (CMD_OUT, "Не вдалося зберегти конфігурацію після скидання\n");
        return 1;
    }
    fprintf (CMD_OUT, "Налаштування скинуто до типових значень.\n");
    return 0;
}

/**
 * @brief Застосування пар ключ=значення до конфігурації та збереження.
 * @param set_pairs Кома-розділений список key=value.
 * @param inout_cfg Конфіг (NULL — авто-завантаження/збереження).
 * @param verbose Докладні журнали.
 * @return 0 — успіх, інакше помилка.
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
        if (cmd_config_apply_pair (inout_cfg, key, value) != 0) {
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
 * @brief Публічний фасад для `device list`.
 * @param model Модель профілю (опційно).
 * @param verbose Докладний режим.
 * @return 0 — успіх, інакше помилка.
 */
cmd_result_t cmd_device_list (const char *model, bool verbose) {
    return cmd_device_list_local (model, verbose ? VERBOSE_ON : VERBOSE_OFF);
}

/**
 * @brief Публічний фасад для друку профілю пристрою.
 * @param alias Псевдонім/порт пристрою (опційно).
 * @param model Очікувана модель профілю (опційно).
 * @param verbose Докладний режим.
 * @return 0 — успіх, інакше помилка.
 */
cmd_result_t cmd_device_profile (const char *alias, const char *model, bool verbose) {
    device_profile_info_t info;
    if (!cmd_device_profile_local (alias, model, &info, verbose ? VERBOSE_ON : VERBOSE_OFF))
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

/**
 * @brief Розвʼязує порт за псевдонімом та друкує повідомлення про помилку у stdout.
 * @return 0 — успіх, 1 — помилка/не знайдено.
 */
static int cmd_resolve_port_or_err (const char *alias, char *out, size_t outlen) {
    int r = cmd_resolve_device_port (alias && *alias ? alias : NULL, out, outlen, NULL, 0, NULL);
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
        if (cmd_resolve_port_or_err (alias, port_buf, sizeof (port_buf)) != 0)                         \
            return 1;                                                                              \
        return cmd_with_axidraw_device (                                                           \
            port_buf, model, verbose ? VERBOSE_ON : VERBOSE_OFF, action_desc, cb, NULL,           \
            wait_idle);                                                                           \
    }

DEV_WRAP (cmd_device_pen_up, "підйом пера", cmd_device_pen_up_cb, true)
DEV_WRAP (cmd_device_pen_down, "опускання пера", cmd_device_pen_down_cb, true)
DEV_WRAP (cmd_device_pen_toggle, "перемикання пера", cmd_device_pen_toggle_cb, true)
DEV_WRAP (cmd_device_motors_on, "увімкнення моторів", cmd_device_motors_on_cb, false)
DEV_WRAP (cmd_device_motors_off, "вимкнення моторів", cmd_device_motors_off_cb, false)
DEV_WRAP (cmd_device_abort, "аварійна зупинка", cmd_device_abort_cb, false)
DEV_WRAP (cmd_device_home, "home", cmd_device_home_cb, true)
DEV_WRAP (cmd_device_version, "версія", cmd_device_version_cb, false)
DEV_WRAP (cmd_device_status, "статус", cmd_device_status_cb, false)
DEV_WRAP (cmd_device_position, "позиція", cmd_device_position_cb, false)
DEV_WRAP (cmd_device_reset, "скидання", cmd_device_reset_cb, false)
DEV_WRAP (cmd_device_reboot, "перезавантаження", cmd_device_reboot_cb, false)

/**
 * @brief Публічний фасад для ручного зсуву (jog) на X/Y мм.
 * @param alias Псевдонім/порт (опційно).
 * @param model Модель (опційно).
 * @param dx_mm Зсув X (мм).
 * @param dy_mm Зсув Y (мм).
 * @param verbose Докладний режим.
 * @return 0 — успіх, інакше помилка.
 */
cmd_result_t
cmd_device_jog (const char *alias, const char *model, double dx_mm, double dy_mm, bool verbose) {
    char port_buf[PATH_MAX];
    if (cmd_resolve_port_or_err (alias, port_buf, sizeof (port_buf)) != 0)
        return 1;
    jog_ctx_t ctx = { .dx_mm = dx_mm, .dy_mm = dy_mm };
    return cmd_with_axidraw_device (
        port_buf, model, verbose ? VERBOSE_ON : VERBOSE_OFF, "ручний зсув", cmd_device_jog_cb, &ctx,
        false);
}
