/**
 * @file hud.c
 * @brief Формування табличного HUD стану AxiDraw.
 */
#include "hud.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#define HUD_COL_WIDTH 30

/**
 * @brief Кешоване представлення значень HUD для порівняння між рендерами.
 */
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
    char exec_time[48];
    char progress_pct[48];
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
static bool g_stdout_is_tty = false;

static hud_view_t g_last_view;
static bool g_last_valid = false;
static bool g_command_running = false;
static struct timespec g_command_start;

/**
 * @brief Очистити поточний рядок термінала перед виведенням HUD.
 */
static void hud_clear_line (void) {
    if (!g_stdout_is_tty)
        return;
    fputs ("\x1b[1G\x1b[2K", stdout); /* у початок рядка та очистити його */
}

/**
 * @brief Скопіювати текст однієї колонки із урахуванням багатобайтних символів.
 *
 * Забезпечує, що результат містить лише цілісні UTF-8 символи та займає не
 * більше HUD_COL_WIDTH моноширинних позицій.
 *
 * @param dst    Буфер призначення (не NULL).
 * @param dst_sz Розмір буфера у байтах.
 * @param src    Вхідний текст (може бути NULL → порожньо).
 * @return Кількість зайнятих позицій у колонці.
 */
static int hud_copy_column_text (char *dst, size_t dst_sz, const char *src) {
    if (!dst || dst_sz == 0)
        return 0;

    const char *input = (src && *src) ? src : "";
    mbstate_t st;
    memset (&st, 0, sizeof (st));
    char *out = dst;
    size_t remaining = dst_sz - 1;
    int used_cols = 0;

    while (*input && remaining > 0) {
        wchar_t wc;
        size_t consumed = mbrtowc (&wc, input, MB_CUR_MAX, &st);
        if (consumed == (size_t)-2 || consumed == (size_t)-1) {
            /* Некоректна послідовність: трактуємо байт як окремий символ. */
            memset (&st, 0, sizeof (st));
            wc = (unsigned char)*input;
            consumed = 1;
        } else if (consumed == 0) {
            break;
        }

        int width = wcwidth (wc);
        if (width < 0)
            width = 1;
        if (used_cols + width > HUD_COL_WIDTH)
            break;
        if (consumed > remaining)
            break;

        memcpy (out, input, consumed);
        out += consumed;
        remaining -= consumed;
        used_cols += width;
        input += consumed;
    }

    *out = '\0';
    return used_cols;
}

/**
 * @brief Вивести текст колонки з вирівнюванням ліворуч та доповненням пробілами.
 *
 * @param text Рядок, що буде надрукований (може бути NULL).
 */
static void hud_print_column (const char *text) {
    char buffer[HUD_COL_WIDTH * 4 + 1];
    int used = hud_copy_column_text (buffer, sizeof (buffer), text);
    fputc (' ', stdout);
    fputs (buffer, stdout);
    for (int i = used; i < HUD_COL_WIDTH; ++i)
        fputc (' ', stdout);
    fputc (' ', stdout);
}

/**
 * @brief Різниця між часовими мітками у секундах.
 */
static double timespec_diff_sec (const struct timespec *a, const struct timespec *b) {
    if (!a || !b)
        return 0.0;
    double sec = (double)(a->tv_sec - b->tv_sec);
    double nsec = (double)(a->tv_nsec - b->tv_nsec) / 1e9;
    return sec + nsec;
}

/**
 * @brief Символи рамки для відображення HUD у UTF-8.
 */
#define HUD_VERT "│"
#define HUD_HORIZ "─"
#define HUD_TOP_LEFT "┌"
#define HUD_TOP_MID "┬"
#define HUD_TOP_RIGHT "┐"
#define HUD_MID_LEFT "├"
#define HUD_MID_MID "┼"
#define HUD_MID_RIGHT "┤"
#define HUD_BOTTOM_LEFT "└"
#define HUD_BOTTOM_MID "┴"
#define HUD_BOTTOM_RIGHT "┘"

typedef enum {
    HUD_BORDER_TOP,
    HUD_BORDER_MID,
    HUD_BORDER_BOTTOM,
} hud_border_t;

/**
 * @brief Надрукувати горизонтальний розділювач HUD із заданими кутами.
 *
 * @param type Тип рамки (верхня, проміжна або нижня).
 */
static void hud_border (hud_border_t type) {
    hud_clear_line ();
    const char *left = NULL;
    const char *mid = NULL;
    const char *right = NULL;

    switch (type) {
    case HUD_BORDER_TOP:
        left = HUD_TOP_LEFT;
        mid = HUD_TOP_MID;
        right = HUD_TOP_RIGHT;
        break;
    case HUD_BORDER_MID:
        left = HUD_MID_LEFT;
        mid = HUD_MID_MID;
        right = HUD_MID_RIGHT;
        break;
    case HUD_BORDER_BOTTOM:
    default:
        left = HUD_BOTTOM_LEFT;
        mid = HUD_BOTTOM_MID;
        right = HUD_BOTTOM_RIGHT;
        break;
    }

    fputs (left, stdout);
    for (int col = 0; col < 3; ++col) {
        for (int i = 0; i < HUD_COL_WIDTH + 2; ++i)
            fputs (HUD_HORIZ, stdout);
        if (col < 2)
            fputs (mid, stdout);
    }
    fputs (right, stdout);
    fputc ('\n', stdout);
}

/**
 * @brief Вивести один рядок HUD з трьома колонками фіксованої ширини.
 *
 * @param a Текст для першої колонки.
 * @param b Текст для другої колонки.
 * @param c Текст для третьої колонки.
 */
static void hud_line (const char *a, const char *b, const char *c) {
    hud_clear_line ();
    fputs (HUD_VERT, stdout);
    hud_print_column (a);
    fputs (HUD_VERT, stdout);
    hud_print_column (b);
    fputs (HUD_VERT, stdout);
    hud_print_column (c);
    fputs (HUD_VERT, stdout);
    fputc ('\n', stdout);
}

/**
 * @brief Сформатувати булеве значення у вигляді тексту з урахуванням відсутніх даних.
 *
 * @param buf        Буфер призначення.
 * @param n          Розмір буфера у байтах.
 * @param valid      Ознака, що значення відоме.
 * @param value      Булеве значення.
 * @param true_word  Рядок для true (може бути NULL → "так").
 * @param false_word Рядок для false (може бути NULL → "ні").
 */
static void format_bool (char *buf, size_t n, bool valid, bool value, const char *true_word,
    const char *false_word) {
    const char *true_s = true_word ? true_word : "так";
    const char *false_s = false_word ? false_word : "ні";
    snprintf (buf, n, "%s", valid ? (value ? true_s : false_s) : "--");
}

/**
 * @brief Сформатувати ціле значення або резервний маркер "--".
 */
static void format_int (char *buf, size_t n, bool valid, long long value) {
    if (!valid)
        snprintf (buf, n, "--");
    else
        snprintf (buf, n, "%lld", value);
}

/**
 * @brief Сформатувати число з плаваючою крапкою або резервний маркер.
 */
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

/**
 * @brief Повернути рядок або "--", якщо значення порожнє.
 */
static const char *safe_str (const char *s) { return (s && *s) ? s : "--"; }

/**
 * @brief Відобразити попередньо побудовану таблицю HUD.
 *
 * @param view Структура з підготовленими текстовими рядками.
 */
static void hud_render_view (const hud_view_t *view) {
    hud_border (HUD_BORDER_TOP);
    hud_line ("ПРИСТРІЙ", "З’ЄДНАННЯ", "КОНФІГУРАЦІЯ");
    hud_border (HUD_BORDER_MID);
    hud_line (view->model, view->status, view->orientation);
    hud_line (view->firmware, view->port, view->paper);
    hud_line (view->fifo_limit, view->baud, view->margins);
    hud_line (view->min_interval, view->timeout, view->speed);
    hud_line ("", "", view->accel);
    hud_border (HUD_BORDER_MID);
    hud_line ("РУХ", "ПОЗИЦІЯ", "ПЕРО / СЕРВО");
    hud_border (HUD_BORDER_MID);
    hud_line (view->command_active, view->steps_x, view->pen);
    hud_line (view->motors, view->steps_y, view->servo_power);
    hud_line (view->exec_time, view->position, view->servo_target);
    hud_line (view->progress_pct, view->fifo_status, view->servo_timeout);
    hud_border (HUD_BORDER_MID);
    hud_line (view->updated, view->phase, view->action);
    hud_line ("", view->rc, view->wait);
    hud_border (HUD_BORDER_BOTTOM);
}

/**
 * @brief Побудувати текстове представлення HUD на основі поточного стану.
 *
 * @param out   Вихідна структура для заповнення.
 * @param state Поточний стан; може бути NULL для "невідомо".
 */
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
    } else {
        snprintf (out->fifo_limit, sizeof (out->fifo_limit), "Ліміт FIFO: --");
        snprintf (out->baud, sizeof (out->baud), "Швидкість: --");
        snprintf (out->min_interval, sizeof (out->min_interval), "Мін. інтервал: --");
        snprintf (out->timeout, sizeof (out->timeout), "Тайм-аут: --");
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

    if (dev) {
        if (dev->max_fifo_commands > 0) {
            double pct = 0.0;
            if (dev->max_fifo_commands > 0)
                pct = ((double)dev->pending_commands * 100.0) / (double)dev->max_fifo_commands;
            if (pct < 0.0)
                pct = 0.0;
            snprintf (
                out->progress_pct, sizeof (out->progress_pct), "FIFO заповнено: %zu/%zu (%.0f%%)",
                dev->pending_commands,
                dev->max_fifo_commands,
                pct);
        } else {
            snprintf (
                out->progress_pct, sizeof (out->progress_pct), "FIFO заповнено: %zu",
                dev->pending_commands);
        }
    } else {
        snprintf (out->progress_pct, sizeof (out->progress_pct), "FIFO заповнено: --");
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

    double elapsed_sec = -1.0;
    if (have_state && snapshot_valid && snap->motion.command_active) {
        if (!g_command_running) {
            g_command_running = true;
            g_command_start = state->ts;
        }
        elapsed_sec = timespec_diff_sec (&state->ts, &g_command_start);
        if (elapsed_sec < 0.0)
            elapsed_sec = 0.0;
    } else {
        g_command_running = false;
    }

    if (elapsed_sec >= 0.0)
        snprintf (out->exec_time, sizeof (out->exec_time), "Час виконання: %.1f с", elapsed_sec);
    else
        snprintf (out->exec_time, sizeof (out->exec_time), "Час виконання: --");

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

/** @copydoc hud_reset */
void hud_reset (void) { g_last_valid = false; }

/** @copydoc hud_set_sources */
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
    g_stdout_is_tty = isatty (fileno (stdout));
}

/** @copydoc hud_render */
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
        bool restore_cursor = g_stdout_is_tty && g_last_valid;
        if (g_stdout_is_tty) {
            if (restore_cursor)
                fputs ("\x1b[s", stdout); /* зберегти позицію курсора */
            fputs ("\x1b[H", stdout); /* перейти у верхній лівий кут */
        }

        hud_render_view (&current);

        if (g_stdout_is_tty && restore_cursor)
            fputs ("\x1b[u", stdout); /* повернутися до попередньої позиції */
        fflush (stdout);
        g_last_view = current;
        g_last_valid = true;
        return true;
    }
    return false;
}
