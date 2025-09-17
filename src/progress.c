/**
 * @file progress.c
 * @brief Реалізація простого прогрес‑бару для CLI з кольорами, Юнікодом та ETA.
 *
 * Модуль надає створення, оновлення та завершення індикатора прогресу з
 * підтримкою «спінера», відсотків, швидкості (кроків/с) та розрахунку ETA.
 */
#include "progress.h"
#include "trace.h"

#include <math.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>

/**
 * Отримати поточний час у секундах (подвійна точність).
 *
 * @return Кількість секунд від епохи.
 */
static double now_s (void) {
    struct timeval tv;
    gettimeofday (&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1e6;
}

static const char *spinner_ascii[] = { "-", "\\", "|", "/" };
static const char *spinner_utf8[] = { "⠋", "⠙", "⠸", "⠴", "⠦", "⠇" };

/**
 * Встановити типові значення опцій відображення прогресу.
 *
 * @param o Структура опцій для ініціалізації (не NULL).
 */
static void opts_defaults (progress_opts_t *o) {
    o->use_colors = true;
    o->use_unicode = true;
    o->width = 40;
    o->throttle_ms = 80;
    o->stream = stderr;
}

/**
 * Створити й ініціалізувати індикатор прогресу.
 *
 * @param p     Об'єкт прогресу (вихід; не NULL).
 * @param total Загальна кількість кроків (0, якщо невідомо).
 * @param opt   Необов'язкові опції; якщо NULL — беруться типові значення.
 */
void progress_init (progress_t *p, uint64_t total, const progress_opts_t *opt) {
    if (!p)
        return;
    memset (p, 0, sizeof (*p));
    progress_opts_t o;
    if (opt) {
        o = *opt;
        if (o.width <= 0)
            o.width = 40;
        if (o.throttle_ms <= 0)
            o.throttle_ms = 80;
        if (!o.stream)
            o.stream = stderr;
    } else {
        opts_defaults (&o);
    }
    p->opt = o;
    p->total = total;
    p->done = 0;
    p->spinner_idx = 0;
    p->finished = false;
    p->started_at = now_s ();
    p->last_draw = 0;
    trace_write (
        LOG_DEBUG,
        "progress: старт total=%llu width=%d throttle=%d colors=%d unicode=%d",
        (unsigned long long)p->total,
        p->opt.width,
        p->opt.throttle_ms,
        p->opt.use_colors ? 1 : 0,
        p->opt.use_unicode ? 1 : 0);
}

/**
 * Відформатувати тривалість у вигляді HH:MM:SS або MM:SS.
 *
 * @param sec Кількість секунд (може бути неціле; <0 буде обрізано до 0).
 * @param buf Буфер виводу.
 * @param n   Розмір буфера.
 */
static void fmt_hhmmss (double sec, char *buf, size_t n) {
    if (sec < 0)
        sec = 0;
    int s = (int)round (sec);
    int h = s / 3600;
    int m = (s % 3600) / 60;
    s = s % 60;
    if (h > 0)
        snprintf (buf, n, "%d:%02d:%02d", h, m, s);
    else
        snprintf (buf, n, "%02d:%02d", m, s);
}

/**
 * Перемалювати індикатор прогресу, дотримуючись налаштованого throttle.
 *
 * Нічого не робить, якщо прогрес завершено або ще не настав інтервал
 * між оновленнями. Виводить рядок, що містить спінер, смугу заповнення,
 * відсоток виконання (за наявності total), швидкість та ETA.
 *
 * @param p Прогрес (не NULL).
 */
void progress_draw (progress_t *p) {
    if (!p || p->finished)
        return;

    double t = now_s ();
    double dt_ms = (t - p->last_draw) * 1000.0;
    if (dt_ms < (double)p->opt.throttle_ms)
        return; /* пропустити зайві оновлення */

    p->last_draw = t;

    FILE *out = p->opt.stream ? p->opt.stream : stderr;
    const char *spin
        = p->opt.use_unicode
              ? spinner_utf8[p->spinner_idx % (int)(sizeof spinner_utf8 / sizeof spinner_utf8[0])]
              : spinner_ascii
                    [p->spinner_idx % (int)(sizeof spinner_ascii / sizeof spinner_ascii[0])];
    p->spinner_idx++;

    double elapsed = t - p->started_at;
    double rate = (elapsed > 0) ? (double)p->done / elapsed : 0.0; /* кроків/с */

    int width = (p->opt.width > 0) ? p->opt.width : 40;
    int bar_w = width;

    // Сформувати прогрес-бар
    char bar[256];
    if (bar_w > (int)sizeof (bar) - 1)
        bar_w = (int)sizeof (bar) - 1;

    double frac = (p->total > 0) ? (double)p->done / (double)p->total : 0.0;
    if (frac < 0)
        frac = 0;
    if (frac > 1)
        frac = 1;
    int filled = (int)round (frac * bar_w);
    if (filled > bar_w)
        filled = bar_w;

    // Юнікод блоки чи ASCII '#'
    const char *full = p->opt.use_unicode ? "█" : "#";
    const char *empty = p->opt.use_unicode ? "░" : "-";
    int pos = 0;
    for (int i = 0; i < bar_w; ++i) {
        const char *sym = (i < filled) ? full : empty;
        int len = (int)strlen (sym);
        if (pos + len >= (int)sizeof (bar))
            break;
        memcpy (bar + pos, sym, (size_t)len);
        pos += len;
    }
    bar[pos] = '\0';

    char eta[32] = "--:--";
    if (p->total > 0 && p->done > 0) {
        double left = ((double)p->total - (double)p->done) / (rate > 1e-9 ? rate : 1e-9);
        fmt_hhmmss (left, eta, sizeof eta);
    }

    int percent = (p->total > 0) ? (int)round (frac * 100.0) : -1;

    if (p->opt.use_colors) {
        fprintf (
            out, "\r%s%s%s %s %s%3s%s %s%.1f/s%s ETA %s", BLUE, spin, NO_COLOR, bar, CYAN,
            (percent >= 0)
                ? (char[5]){ (char)((percent / 100) + '0'), (char)(((percent / 10) % 10) + '0'),
                             (char)((percent % 10) + '0'), '%', '\0' }
                : " --%",
            NO_COLOR, MAGENTA, rate, NO_COLOR, eta);
    } else {
        fprintf (
            out, "\r%s %s %3s %.1f/s ETA %s", spin, bar,
            (percent >= 0)
                ? (char[5]){ (char)((percent / 100) + '0'), (char)(((percent / 10) % 10) + '0'),
                             (char)((percent % 10) + '0'), '%', '\0' }
                : " --%",
            rate, eta);
    }
    fflush (out);
}

/**
 * Додати кроки до виконаних і, за потреби, намалювати прогрес.
 *
 * @param p     Прогрес (не NULL).
 * @param delta Приріст виконаних кроків.
 */
void progress_add (progress_t *p, uint64_t delta) {
    if (!p || p->finished)
        return;
    p->done += delta;
    progress_draw (p);
}

/**
 * Завершити індикатор прогресу: фінальне перемальовування (якщо потрібно)
 * і перенесення рядка.
 *
 * @param p Прогрес (не NULL).
 */
void progress_finish (progress_t *p) {
    if (!p)
        return;
    if (!p->finished)
        progress_draw (p);
    p->finished = true;
    FILE *out = p->opt.stream ? p->opt.stream : stderr;
    fprintf (out, "\n");
    fflush (out);
    trace_write (
        LOG_INFO,
        "progress: завершено done=%llu total=%llu",
        (unsigned long long)p->done,
        (unsigned long long)p->total);
}
