/**
 * @file hud.c
 * @brief Формування табличного HUD стану AxiDraw.
 */
#include "hud.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define HUD_COL_WIDTH 30

typedef struct {
    char model[48];
    char status[48];
    char orientation[48];
    char firmware[48];
    char port[48];
    char paper[48];
    char fifo_limit[48];
    char baud[48];
    char margins[64];
    char min_interval[48];
    char timeout[48];
    char speed[48];
    char accel[48];
    char command_active[48];
    char motors[48];
    char fifo_status[48];
    char position[64];
    char steps_x[48];
    char steps_y[48];
    char pen[48];
    char servo_power[48];
    char servo_target[48];
    char servo_timeout[48];
    char queue[48];
    char updated[64];
    char phase[48];
    char action[48];
    char rc[32];
    char wait[32];
    bool snapshot_valid;
} hud_view_t;

static const axidraw_device_t *g_device = NULL;
static const axidraw_settings_t *g_settings = NULL;
static const config_t *g_config = NULL;
static char g_model[32];
static bool g_context_ready = false;

static hud_view_t g_last_view;
static bool g_last_valid = false;

static void hud_separator (void) {
    fputc ('+', stdout);
    for (int col = 0; col < 3; ++col) {
        for (int i = 0; i < HUD_COL_WIDTH + 2; ++i)
            fputc ('-', stdout);
        fputc ('+', stdout);
    }
    fputc ('\n', stdout);
}

static void hud_line (const char *a, const char *b, const char *c) {
    printf (
        "| %-*.*s | %-*.*s | %-*.*s |\n",
        HUD_COL_WIDTH,
        HUD_COL_WIDTH,
        a ? a : "",
        HUD_COL_WIDTH,
        HUD_COL_WIDTH,
        b ? b : "",
        HUD_COL_WIDTH,
        HUD_COL_WIDTH,
        c ? c : "");
}

static void format_bool (char *buf, size_t n, bool valid, bool value, const char *true_word,
    const char *false_word) {
    const char *true_s = true_word ? true_word : "так";
    const char *false_s = false_word ? false_word : "ні";
    snprintf (buf, n, "%s", valid ? (value ? true_s : false_s) : "--");
}

static void format_int (char *buf, size_t n, bool valid, long long value) {
    if (!valid)
        snprintf (buf, n, "--");
    else
        snprintf (buf, n, "%lld", value);
}

static void format_double (char *buf, size_t n, bool valid, double value, int decimals) {
    if (!valid || !isfinite (value))
        snprintf (buf, n, "--");
    else {
        switch (decimals) {
        case 0:
            snprintf (buf, n, "%.0f", value);
            break;
        case 1:
            snprintf (buf, n, "%.1f", value);
            break;
        case 2:
            snprintf (buf, n, "%.2f", value);
            break;
        default:
            snprintf (buf, n, "%.3f", value);
            break;
        }
    }
}

static const char *safe_str (const char *s) { return (s && *s) ? s : "--"; }

static void hud_render_view (const hud_view_t *view) {
    hud_separator ();
    hud_line ("ПРИСТРІЙ", "З’ЄДНАННЯ", "КОНФІГУРАЦІЯ");
    hud_separator ();
    hud_line (view->model, view->status, view->orientation);
    hud_line (view->firmware, view->port, view->paper);
    hud_line (view->fifo_limit, view->baud, view->margins);
    hud_line (view->min_interval, view->timeout, view->speed);
    hud_line ("", "", view->accel);
    hud_separator ();
    hud_line ("РУХ", "ПОЗИЦІЯ", "ПЕРО / СЕРВО");
    hud_separator ();
    hud_line (view->command_active, view->steps_x, view->pen);
    hud_line (view->motors, view->steps_y, view->servo_power);
    hud_line (view->fifo_status, view->position, view->servo_target);
    hud_line (view->queue, "", view->servo_timeout);
    hud_separator ();
    hud_line (view->updated, view->phase, view->action);
    hud_line ("", view->rc, view->wait);
    hud_separator ();
    fflush (stdout);
}

static void hud_build_view (hud_view_t *out, const axistate_t *state) {
    memset (out, 0, sizeof (*out));
    bool have_state = state && state->valid;
    bool snapshot_valid = have_state && state->snapshot_valid;
    const ebb_status_snapshot_t *snap = snapshot_valid ? &state->snapshot : NULL;
    const axidraw_device_t *dev = g_device;
    const axidraw_settings_t *settings = g_settings;
    const config_t *cfg = g_config;

    snprintf (out->model, sizeof (out->model), "Модель: %s", safe_str (g_model));
    snprintf (out->status, sizeof (out->status), "Статус: %s",
        (dev && axidraw_device_is_connected (dev)) ? "підключено" : "немає з’єднання");
    snprintf (out->orientation, sizeof (out->orientation), "Орієнтація: %s",
        cfg ? ((cfg->orientation == ORIENT_LANDSCAPE) ? "альбом" : "портрет") : "--");

    snprintf (out->firmware, sizeof (out->firmware), "Прошивка: %s",
        (snap && snap->firmware[0]) ? snap->firmware : "--");
    snprintf (out->port, sizeof (out->port), "Порт: %s",
        (dev && dev->port_path[0]) ? dev->port_path : "--");
    snprintf (out->paper, sizeof (out->paper), "Папір (мм): %s",
        cfg ? "" : "--");
    if (cfg)
        snprintf (out->paper, sizeof (out->paper), "Папір (мм): %.1f × %.1f", cfg->paper_w_mm, cfg->paper_h_mm);

    if (dev) {
        if (dev->max_fifo_commands == 0)
            snprintf (out->fifo_limit, sizeof (out->fifo_limit), "Ліміт FIFO: без обмеж.");
        else
            snprintf (out->fifo_limit, sizeof (out->fifo_limit), "Ліміт FIFO: %zu", dev->max_fifo_commands);
        snprintf (out->baud, sizeof (out->baud), "Швидкість: %d", dev->baud);
        snprintf (out->min_interval, sizeof (out->min_interval), "Мін. інтервал: %.1f мс", dev->min_cmd_interval);
        snprintf (out->timeout, sizeof (out->timeout), "Тайм-аут: %d мс", dev->timeout_ms);
        if (dev->max_fifo_commands == 0)
            snprintf (out->queue, sizeof (out->queue), "Черга команд: %zu/inf", dev->pending_commands);
        else
            snprintf (out->queue, sizeof (out->queue), "Черга команд: %zu/%zu", dev->pending_commands,
                dev->max_fifo_commands);
    } else {
        snprintf (out->fifo_limit, sizeof (out->fifo_limit), "Ліміт FIFO: --");
        snprintf (out->baud, sizeof (out->baud), "Швидкість: --");
        snprintf (out->min_interval, sizeof (out->min_interval), "Мін. інтервал: --");
        snprintf (out->timeout, sizeof (out->timeout), "Тайм-аут: --");
        snprintf (out->queue, sizeof (out->queue), "Черга команд: --");
    }

    if (cfg)
        snprintf (out->margins, sizeof (out->margins), "Поля (мм): %.1f/%.1f/%.1f/%.1f",
            cfg->margin_top_mm,
            cfg->margin_right_mm,
            cfg->margin_bottom_mm,
            cfg->margin_left_mm);
    else
        snprintf (out->margins, sizeof (out->margins), "Поля (мм): --");

    if (settings) {
        snprintf (out->speed, sizeof (out->speed), "Швидкість: %.1f мм/с", settings->speed_mm_s);
        snprintf (out->accel, sizeof (out->accel), "Прискорення: %.1f мм/с²", settings->accel_mm_s2);
    } else {
        snprintf (out->speed, sizeof (out->speed), "Швидкість: --");
        snprintf (out->accel, sizeof (out->accel), "Прискорення: --");
    }

    char pen_state[16];
    format_bool (
        pen_state, sizeof (pen_state), snapshot_valid, snap && snap->pen_up, "вгору", "вниз");
    snprintf (out->pen, sizeof (out->pen), "Перо: %s", pen_state);

    char servo_power[16];
    format_bool (servo_power, sizeof (servo_power), snapshot_valid, snap && snap->servo_power, "так", "ні");
    snprintf (out->servo_power, sizeof (out->servo_power), "Сервопривід: %s", servo_power);

    if (settings && snapshot_valid) {
        int target = snap->pen_up ? settings->pen_up_pos : settings->pen_down_pos;
        if (target >= 0)
            snprintf (out->servo_target, sizeof (out->servo_target), "Ціль серво: %d%%", target);
        else
            snprintf (out->servo_target, sizeof (out->servo_target), "Ціль серво: --");
    } else {
        snprintf (out->servo_target, sizeof (out->servo_target), "Ціль серво: --");
    }

    if (settings) {
        if (settings->servo_timeout_s < 0)
            snprintf (out->servo_timeout, sizeof (out->servo_timeout), "Тайм-аут серво: вимкнено");
        else
            snprintf (out->servo_timeout, sizeof (out->servo_timeout), "Тайм-аут серво: %d с",
                settings->servo_timeout_s);
    } else {
        snprintf (out->servo_timeout, sizeof (out->servo_timeout), "Тайм-аут серво: --");
    }

    if (snapshot_valid) {
        char command[16];
        format_bool (command, sizeof (command), true, snap->motion.command_active, "так", "ні");
        snprintf (out->command_active, sizeof (out->command_active), "Команда активна: %s", command);

        snprintf (out->motors, sizeof (out->motors), "Мотори: X:%s Y:%s",
            snap->motion.motor1_active ? "так" : "ні",
            snap->motion.motor2_active ? "так" : "ні");

        snprintf (out->fifo_status, sizeof (out->fifo_status), "FIFO: %s",
            snap->motion.fifo_pending ? "зайнято" : "порожнє");

        char pos_x[32];
        char pos_y[32];
        format_double (pos_x, sizeof (pos_x), true, snap->steps_axis1 / AXIDRAW_STEPS_PER_MM, 3);
        format_double (pos_y, sizeof (pos_y), true, snap->steps_axis2 / AXIDRAW_STEPS_PER_MM, 3);
        snprintf (out->position, sizeof (out->position), "Позиція (мм): X:%s Y:%s", pos_x, pos_y);

        char steps_x[32];
        char steps_y[32];
        format_int (steps_x, sizeof (steps_x), true, snap->steps_axis1);
        format_int (steps_y, sizeof (steps_y), true, snap->steps_axis2);
        snprintf (out->steps_x, sizeof (out->steps_x), "Кроки X: %s", steps_x);
        snprintf (out->steps_y, sizeof (out->steps_y), "Кроки Y: %s", steps_y);
    } else {
        snprintf (out->command_active, sizeof (out->command_active), "Команда активна: --");
        snprintf (out->motors, sizeof (out->motors), "Мотори: --");
        snprintf (out->fifo_status, sizeof (out->fifo_status), "FIFO: --");
        snprintf (out->position, sizeof (out->position), "Позиція (мм): X:-- Y:--");
        snprintf (out->steps_x, sizeof (out->steps_x), "Кроки X: --");
        snprintf (out->steps_y, sizeof (out->steps_y), "Кроки Y: --");
    }

    if (have_state) {
        struct tm tm_buf;
        char ts_buf[32];
        time_t sec = state->ts.tv_sec;
        if (localtime_r (&sec, &tm_buf))
            strftime (ts_buf, sizeof (ts_buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
        else
            snprintf (ts_buf, sizeof (ts_buf), "%lld", (long long)sec);
        long millis = state->ts.tv_nsec / 1000000L;
        if (millis < 0)
            millis = 0;
        snprintf (out->updated, sizeof (out->updated), "Оновлено: %s.%03ld", ts_buf, millis);
        snprintf (out->phase, sizeof (out->phase), "Фаза: %s",
            state->phase[0] ? state->phase : "--");
        snprintf (out->action, sizeof (out->action), "Дія: %s",
            state->action[0] ? state->action : "--");
        snprintf (out->rc, sizeof (out->rc), "RC: %d", state->command_rc);
        snprintf (out->wait, sizeof (out->wait), "Очікування: %d", state->wait_rc);
    } else {
        snprintf (out->updated, sizeof (out->updated), "Оновлено: --");
        snprintf (out->phase, sizeof (out->phase), "Фаза: --");
        snprintf (out->action, sizeof (out->action), "Дія: --");
        snprintf (out->rc, sizeof (out->rc), "RC: --");
        snprintf (out->wait, sizeof (out->wait), "Очікування: --");
    }

    out->snapshot_valid = snapshot_valid;
}

void hud_reset (void) { g_last_valid = false; }

void hud_set_sources (
    const axidraw_device_t *device,
    const axidraw_settings_t *settings,
    const config_t *cfg,
    const char *model) {
    g_device = device;
    g_settings = settings;
    g_config = cfg;
    if (model && *model) {
        strncpy (g_model, model, sizeof (g_model) - 1);
        g_model[sizeof (g_model) - 1] = '\0';
    } else {
        snprintf (g_model, sizeof (g_model), "--");
    }
    g_context_ready = true;
}

bool hud_render (const axistate_t *state_in, bool force) {
    if (!g_context_ready)
        return false;

    axistate_t local_state;
    const axistate_t *state_ptr = state_in;
    if (!state_ptr) {
        if (axistate_get (&local_state))
            state_ptr = &local_state;
        else
            state_ptr = NULL;
    }

    hud_view_t current;
    hud_build_view (&current, state_ptr);

    bool changed = force || !g_last_valid || memcmp (&current, &g_last_view, sizeof (current)) != 0;
    if (changed) {
        hud_render_view (&current);
        g_last_view = current;
        g_last_valid = true;
        return true;
    }
    return false;
}
