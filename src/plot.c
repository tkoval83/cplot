/**
 * @file plot.c
 * @brief Виконання плану руху через stepper/axidraw.
 */

#include "plot.h"

#include "axidraw.h"
#include "log.h"
#include "stepper.h"

#include <stdlib.h>
#include <string.h>

/**
 * @brief Заповнює налаштування AxiDraw із профілю моделі.
 *
 * Ініціалізує конфігурацію за замовчуванням для вказаної моделі, застосовує
 * профіль пристрою (швидкість/прискорення/розміри) і переносить релевантні
 * поля до `axidraw_settings_t`. Значення `steps_per_mm` встановлюється з
 * профілю; якщо воно невалідне (≤ 0), функція повертає `false`.
 *
 * @param model Ідентифікатор моделі (NULL — типова модель з CONFIG_DEFAULT_MODEL).
 * @param out [out] Місце призначення для налаштувань AxiDraw.
 * @return true — успіх; false — помилка або некоректний профіль (steps_per_mm ≤ 0).
 */
static bool plot_load_settings (const char *model, axidraw_settings_t *out) {
    if (!out)
        return false;
    const char *model_id = (model && *model) ? model : CONFIG_DEFAULT_MODEL;
    config_t cfg;
    if (config_factory_defaults (&cfg, model_id) != 0)
        return false;
    const axidraw_device_profile_t *profile = axidraw_device_profile_for_model (model_id);
    axidraw_device_profile_apply (&cfg, profile);
    axidraw_settings_reset (out);
    out->pen_up_delay_ms = cfg.pen_up_delay_ms;
    out->pen_down_delay_ms = cfg.pen_down_delay_ms;
    out->pen_up_pos = cfg.pen_up_pos;
    out->pen_down_pos = cfg.pen_down_pos;
    out->pen_up_speed = cfg.pen_up_speed;
    out->pen_down_speed = cfg.pen_down_speed;
    out->servo_timeout_s = cfg.servo_timeout_s;
    out->speed_mm_s = cfg.speed_mm_s;
    out->accel_mm_s2 = cfg.accel_mm_s2;
    if (profile && profile->steps_per_mm > 0.0)
        out->steps_per_mm = profile->steps_per_mm;
    return (out->steps_per_mm > 0.0);
}

/**
 * @brief Виконує послідовність блоків руху на пристрої або імітує (dry‑run).
 *
 * Поведінка:
 *  - Якщо `dry_run == true`, створює локальний екземпляр пристрою з налаштуваннями
 *    профілю (без підключення до серійного порту) і викликає `stepper_submit_block`
 *    для кожного блоку у режимі імітації.
 *  - Якщо `dry_run == false`, захоплює lock‑файл, підключається до пристрою,
 *    вмикає мотори, піднімає перо, далі для кожного блоку перемикає перо за
 *    потреби та передає блок у `stepper_submit_block` для реального руху.
 *    Після виконання піднімає перо, відключається і звільняє lock.
 *
 * @param blocks Масив блоків плану руху.
 * @param count Кількість блоків у плані.
 * @param model Ідентифікатор моделі (NULL — типова).
 * @param dry_run true — імітація без підключення; false — відправка на пристрій.
 * @param verbose true — докладні журнали (зарезервовано; може бути використано надалі).
 * @return 0 — успіх; 1 — помилка налаштувань/підключення/виконання.
 */
int plot_execute_plan (
    const plan_block_t *blocks, size_t count, const char *model, bool dry_run, bool verbose) {
    (void)verbose;
    if (!blocks || count == 0)
        return 0;

    if (dry_run) {
        axidraw_settings_t settings;
        if (!plot_load_settings (model, &settings))
            return 1;
        axidraw_device_t sim;
        axidraw_device_init (&sim);
        axidraw_apply_settings (&sim, &settings);
        stepper_config_t scfg = { .dev = &sim };
        stepper_context_t sc;
        stepper_init (&sc, &scfg);
        for (size_t i = 0; i < count; ++i) {
            if (!stepper_submit_block (&sc, &blocks[i], true))
                return 1;
        }
        return 0;
    }

    int lock_fd = -1;
    if (axidraw_device_lock_acquire (&lock_fd) != 0)
        return 1;

    int status = 0;
    axidraw_settings_t settings;
    if (!plot_load_settings (model, &settings)) {
        status = 1;
        goto cleanup;
    }

    axidraw_device_t dev;
    axidraw_device_init (&dev);
    axidraw_apply_settings (&dev, &settings);
    axidraw_device_config (&dev, NULL, 9600, 5000, settings.min_cmd_interval_ms);

    char err[128];
    if (axidraw_device_connect (&dev, err, sizeof (err)) != 0) {
        LOGE ("Не вдалося підключитися до пристрою: %s", err);
        status = 1;
        goto cleanup_dev;
    }

    (void)axidraw_motors_set_mode (&dev, AXIDRAW_MOTOR_STEP_16, AXIDRAW_MOTOR_STEP_16);
    (void)axidraw_pen_up (&dev);

    stepper_config_t scfg = { .dev = &dev };
    stepper_context_t sc;
    stepper_init (&sc, &scfg);
    bool pen_is_up = true;
    for (size_t i = 0; i < count; ++i) {
        const plan_block_t *blk = &blocks[i];
        if (blk->pen_down && pen_is_up) {
            (void)axidraw_pen_down (&dev);
            pen_is_up = false;
        } else if (!blk->pen_down && !pen_is_up) {
            (void)axidraw_pen_up (&dev);
            pen_is_up = true;
        }
        if (!stepper_submit_block (&sc, blk, false)) {
            status = 1;
            break;
        }
    }
    if (!pen_is_up)
        (void)axidraw_pen_up (&dev);
    (void)axidraw_wait_for_idle (&dev, 2000);

cleanup_dev:
    axidraw_device_disconnect (&dev);
cleanup:
    axidraw_device_lock_release (lock_fd);
    return status;
}

/**
 * @brief Генерує план руху з макета полотна та виконує його (або dry‑run).
 *
 * Викликає `canvas_generate_motion_plan()` для отримання блоків руху з
 * нормалізованих шляхів полотна, далі делегує виконання у `plot_execute_plan()`.
 * Відповідальність за звільнення памʼяті блокується всередині цієї функції.
 *
 * @param layout Макет полотна (кінцеві шляхи у мм).
 * @param model Ідентифікатор моделі (NULL — типова).
 * @param dry_run true — імітація; false — реальне виконання.
 * @param verbose true — докладні журнали (зарезервовано).
 * @return 0 — успіх; 1 — помилка планування або виконання.
 */
int plot_canvas_execute (
    const canvas_layout_t *layout, const char *model, bool dry_run, bool verbose) {
    (void)verbose;
    if (!layout)
        return 1;
    plan_block_t *blocks = NULL;
    size_t count = 0;
    if (canvas_generate_motion_plan (layout, NULL, &blocks, &count) != 0)
        return 1;
    int rc = plot_execute_plan (blocks, count, model, dry_run, verbose);
    free (blocks);
    return rc;
}
