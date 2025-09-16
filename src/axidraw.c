/**
 * @file axidraw.c
 * @brief Реалізація високорівневого менеджера AxiDraw.
 */

#include "axidraw.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "log.h"

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 0
#endif

/**
 * @brief Обчислити різницю між двома позначками часу у мс.
 *
 * @param now  Поточний час.
 * @param prev Попередній час.
 * @return Різниця в мілісекундах.
 */
static double timespec_diff_ms (const struct timespec *now, const struct timespec *prev) {
    double diff = (double)(now->tv_sec - prev->tv_sec) * 1000.0;
    diff += (double)(now->tv_nsec - prev->tv_nsec) / 1e6;
    return diff;
}

/**
 * @brief Скинути timestamp останньої команди.
 *
 * @param dev Структура пристрою.
 */
static void axidraw_reset_timestamp (axidraw_device_t *dev) {
    if (!dev)
        return;
    dev->last_cmd.tv_sec = 0;
    dev->last_cmd.tv_nsec = 0;
}

/** Ініціалізувати структуру dev типовими значеннями. */
void axidraw_device_init (axidraw_device_t *dev) {
    if (!dev)
        return;
    memset (dev, 0, sizeof (*dev));
    dev->baud = 9600;
    dev->timeout_ms = 5000;
    dev->min_cmd_interval = 5.0;
    axidraw_reset_timestamp (dev);
    LOGD (
        "axidraw: init (baud=%d timeout=%d min_interval=%.1f)", dev->baud, dev->timeout_ms,
        dev->min_cmd_interval);
}

/** Налаштувати шлях, швидкість та інтервали перед підключенням. */
void axidraw_device_config (
    axidraw_device_t *dev,
    const char *port_path,
    int baud,
    int timeout_ms,
    double min_cmd_interval_ms) {
    if (!dev)
        return;
    if (port_path)
        strncpy (dev->port_path, port_path, sizeof (dev->port_path) - 1);
    dev->baud = baud > 0 ? baud : 9600;
    dev->timeout_ms = timeout_ms > 0 ? timeout_ms : 5000;
    dev->min_cmd_interval = min_cmd_interval_ms >= 0.0 ? min_cmd_interval_ms : 0.0;
    LOGI (
        "axidraw: config port=%s baud=%d timeout=%d min_interval=%.1f", dev->port_path, dev->baud,
        dev->timeout_ms, dev->min_cmd_interval);
}

/**
 * @brief Автоматично визначити порт AxiDraw (macOS).
 *
 * @param dev Структура пристрою.
 * @param buf Буфер для запису шляху.
 * @param len Розмір буфера.
 * @return 0 при успіху; -1, якщо порт не знайдено.
 */
static int axidraw_guess_port (axidraw_device_t *dev, char *buf, size_t len) {
    if (!dev)
        return -1;
    if (dev->port_path[0])
        return 0;
#ifdef __APPLE__
    if (serial_guess_axidraw_port (buf, len) == 0) {
        strncpy (dev->port_path, buf, sizeof (dev->port_path) - 1);
        dev->port_path[sizeof (dev->port_path) - 1] = '\0';
        LOGI ("axidraw: guessed port %s", dev->port_path);
        return 0;
    }
#endif
    LOGW ("axidraw: не вдалося автоматично знайти порт");
    return -1;
}

/** Спробувати відкрити порт і перевірити зв’язок із EBB. */
int axidraw_device_connect (axidraw_device_t *dev, char *errbuf, size_t errlen) {
    if (!dev)
        return -1;
    if (dev->connected)
        return 0;

    char guess_buf[256] = { 0 };
    if (dev->port_path[0] == '\0') {
        if (axidraw_guess_port (dev, guess_buf, sizeof (guess_buf)) != 0) {
            LOGE ("Не вказано порт AxiDraw");
            return -1;
        }
    }

    char local_err[256];
    char *err_dst = errbuf ? errbuf : local_err;
    size_t err_sz = errbuf ? errlen : sizeof (local_err);
    serial_port_t *sp = serial_open (dev->port_path, dev->baud, dev->timeout_ms, err_dst, err_sz);
    if (!sp) {
        if (!errbuf)
            LOGE ("Не вдалося відкрити порт: %s", err_dst);
        return -1;
    }
    serial_flush_input (sp);
    if (serial_probe_ebb (sp, NULL, 0) != 0) {
        if (!errbuf)
            LOGE ("axidraw: пристрій не відповідає на команду V");
        serial_close (sp);
        return -1;
    }
    dev->port = sp;
    dev->connected = true;
    axidraw_reset_timestamp (dev);
    LOGI ("axidraw: підключено через %s", dev->port_path);
    return 0;
}

/** Закрити порт та скинути статус. */
void axidraw_device_disconnect (axidraw_device_t *dev) {
    if (!dev)
        return;
    if (dev->port) {
        serial_close (dev->port);
        dev->port = NULL;
        LOGI ("axidraw: відключено від %s", dev->port_path);
    }
    dev->connected = false;
    axidraw_reset_timestamp (dev);
}

/** Перевірити, чи пристрій у стані connected. */
bool axidraw_device_is_connected (const axidraw_device_t *dev) { return dev && dev->connected; }

/**
 * @brief Встановити мінімальний інтервал між командами.
 *
 * @param dev Структура пристрою.
 * @param min_interval_ms Інтервал у мілісекундах (>= 0).
 */
void axidraw_set_rate_limit (axidraw_device_t *dev, double min_interval_ms) {
    if (!dev)
        return;
    dev->min_cmd_interval = min_interval_ms >= 0.0 ? min_interval_ms : 0.0;
    LOGI ("axidraw: мін. інтервал між командами %.2f мс", dev->min_cmd_interval);
}

/**
 * @brief Переконатися, що пристрій підключено.
 *
 * @param dev Структура пристрою.
 * @return 0 якщо підключено; -1 інакше.
 */
static int axidraw_require_connection (axidraw_device_t *dev) {
    if (!dev || !dev->connected || !dev->port) {
        LOGE ("axidraw: пристрій не підключено");
        return -1;
    }
    return 0;
}

/**
 * @brief Дочекатися, доки мине мінімальний інтервал між командами.
 *
 * @param dev Структура пристрою.
 */
static void axidraw_wait_slot (axidraw_device_t *dev) {
    if (!dev || dev->min_cmd_interval <= 0.0)
        return;
    if (dev->last_cmd.tv_sec == 0 && dev->last_cmd.tv_nsec == 0)
        return;

    struct timespec now;
    bool logged = false;
    while (1) {
        if (clock_gettime (CLOCK_MONOTONIC, &now) != 0)
            break;
        double elapsed = timespec_diff_ms (&now, &dev->last_cmd);
        if (elapsed >= dev->min_cmd_interval)
            break;
        double remaining = dev->min_cmd_interval - elapsed;
        if (remaining <= 0.0)
            break;
        long ms_whole = (long)remaining;
        long ns_part = (long)((remaining - (double)ms_whole) * 1e6);
        struct timespec ts
            = { .tv_sec = ms_whole / 1000, .tv_nsec = (ms_whole % 1000) * 1000000L + ns_part };
        if (!logged) {
            LOGD ("axidraw: затримка %.2f мс перед наступною командою", remaining);
            logged = true;
        }
        nanosleep (&ts, NULL);
    }
}

/** Зберегти поточний час як мітку останньої команди. */
static void axidraw_stamp (axidraw_device_t *dev) {
    if (!dev)
        return;
    struct timespec now;
    if (clock_gettime (CLOCK_MONOTONIC, &now) == 0)
        dev->last_cmd = now;
}

/**
 * Обгортка для виклику команд ebb_*, що додає rate limiter та логування.
 */
static int axidraw_exec (axidraw_device_t *dev, int (*fn) (serial_port_t *, int), int timeout) {
    if (axidraw_require_connection (dev) != 0)
        return -1;
    axidraw_wait_slot (dev);
    LOGD ("axidraw: виконання команди (timeout=%d)", timeout);
    int rc = fn (dev->port, timeout);
    if (rc == 0)
        axidraw_stamp (dev);
    return rc;
}

/** Підняти перо (SP,1). */
int axidraw_pen_up (axidraw_device_t *dev) {
    return axidraw_exec (dev, ebb_pen_up, dev->timeout_ms);
}

/** Опустити перо (SP,0). */
int axidraw_pen_down (axidraw_device_t *dev) {
    return axidraw_exec (dev, ebb_pen_down, dev->timeout_ms);
}

/**
 * Допоміжний виклик для рухових команд з тривалістю та двома параметрами.
 */
/**
 * @brief Допоміжний виклик для рухових команд з двома параметрами.
 *
 * @param dev      Структура пристрою.
 * @param fn       Функція ebb_move_*.
 * @param duration Тривалість у мс.
 * @param a        Перший параметр (кроки).
 * @param b        Другий параметр (кроки).
 * @return Код повернення fn.
 */
static int axidraw_exec_sm (
    axidraw_device_t *dev,
    int (*fn) (serial_port_t *, uint32_t, int32_t, int32_t, int),
    uint32_t duration,
    int32_t a,
    int32_t b) {
    if (axidraw_require_connection (dev) != 0)
        return -1;
    axidraw_wait_slot (dev);
    LOGD ("axidraw: рух duration=%u a=%d b=%d", duration, a, b);
    int rc = fn (dev->port, duration, a, b, dev->timeout_ms);
    if (rc == 0)
        axidraw_stamp (dev);
    else
        LOGE ("axidraw: команда руху повернула помилку (%d)", rc);
    return rc;
}

/** Виконати SM у координатах X/Y. */
int axidraw_move_xy (
    axidraw_device_t *dev, uint32_t duration_ms, int32_t steps_x, int32_t steps_y) {
    return axidraw_exec_sm (dev, ebb_move_steps, duration_ms, steps_x, steps_y);
}

/** Виконати XM (CoreXY/H-bot). */
int axidraw_move_corexy (
    axidraw_device_t *dev, uint32_t duration_ms, int32_t steps_a, int32_t steps_b) {
    if (axidraw_require_connection (dev) != 0)
        return -1;
    axidraw_wait_slot (dev);
    int rc = ebb_move_mixed (dev->port, duration_ms, steps_a, steps_b, dev->timeout_ms);
    if (rc == 0) {
        axidraw_stamp (dev);
    } else {
        LOGE ("axidraw: команда XM повернула помилку (%d)", rc);
    }
    return rc;
}

/** Виконати низькорівневу команду LM. */
int axidraw_move_lowlevel (
    axidraw_device_t *dev,
    uint32_t rate1,
    int32_t steps1,
    int32_t accel1,
    uint32_t rate2,
    int32_t steps2,
    int32_t accel2,
    int clear_flags) {
    if (axidraw_require_connection (dev) != 0)
        return -1;
    axidraw_wait_slot (dev);
    LOGD ("axidraw: LM rate1=%u steps1=%d rate2=%u steps2=%d", rate1, steps1, rate2, steps2);
    int rc = ebb_move_lowlevel_steps (
        dev->port, rate1, steps1, accel1, rate2, steps2, accel2, clear_flags, dev->timeout_ms);
    if (rc == 0) {
        axidraw_stamp (dev);
    } else {
        LOGE ("axidraw: команда LM повернула помилку (%d)", rc);
    }
    return rc;
}

/** Виконати низькорівневу команду LT. */
int axidraw_move_lowlevel_time (
    axidraw_device_t *dev,
    uint32_t intervals,
    int32_t rate1,
    int32_t accel1,
    int32_t rate2,
    int32_t accel2,
    int clear_flags) {
    if (axidraw_require_connection (dev) != 0)
        return -1;
    axidraw_wait_slot (dev);
    LOGD ("axidraw: LT intervals=%u rate1=%d rate2=%d", intervals, rate1, rate2);
    int rc = ebb_move_lowlevel_time (
        dev->port, intervals, rate1, accel1, rate2, accel2, clear_flags, dev->timeout_ms);
    if (rc == 0) {
        axidraw_stamp (dev);
    } else {
        LOGE ("axidraw: команда LT повернула помилку (%d)", rc);
    }
    return rc;
}

/** Виклик HM для повернення в home/абсолютну позицію. */
int axidraw_home (
    axidraw_device_t *dev, uint32_t step_rate, const int32_t *pos1, const int32_t *pos2) {
    if (axidraw_require_connection (dev) != 0)
        return -1;
    axidraw_wait_slot (dev);
    LOGD (
        "axidraw: HM step_rate=%u pos1=%d pos2=%d", step_rate, pos1 ? *pos1 : 0, pos2 ? *pos2 : 0);
    int rc = ebb_home_move (dev->port, step_rate, pos1, pos2, dev->timeout_ms);
    if (rc == 0) {
        axidraw_stamp (dev);
    } else {
        LOGE ("axidraw: команда HM повернула помилку (%d)", rc);
    }
    return rc;
}

/** Зібрати агрегований статус пристрою. */
int axidraw_status (axidraw_device_t *dev, ebb_status_snapshot_t *snapshot) {
    if (axidraw_require_connection (dev) != 0)
        return -1;
    int rc = ebb_collect_status (dev->port, snapshot, dev->timeout_ms);
    if (rc != 0) {
        LOGE ("axidraw: не вдалося зібрати статус (код %d)", rc);
    } else {
        LOGD (
            "axidraw: статус motion=%d/%d/%d fifo=%d pen=%d", snapshot->motion.command_active,
            snapshot->motion.motor1_active, snapshot->motion.motor2_active,
            snapshot->motion.fifo_pending, snapshot->pen_up);
    }
    return rc;
}
