/**
 * @file axidraw.c
 * @brief Реалізація взаємодії з AxiDraw та команд руху.
 * @ingroup axidraw
 */

#include "axidraw.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>

#define AXIDRAW_DEFAULT_FIFO_LIMIT 3
#define AXIDRAW_DEFAULT_MIN_INTERVAL_MS 5.0
#define AXIDRAW_SERVO_MIN 7500
#define AXIDRAW_SERVO_MAX 28000
#define AXIDRAW_SERVO_SPEED_SCALE 5
#define AXIDRAW_MAX_DURATION_MS 16777215u

#include "log.h"
#include "str.h"
#include "ttime.h"

#ifdef DEBUG
#define AXIDRAW_LOG(MSG) "axidraw: " MSG
#else
#define AXIDRAW_LOG(MSG) MSG
#endif

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 0
#endif

static const axidraw_device_profile_t k_axidraw_device_profiles[] = {
    { "minikit2", 160.0, 101.0, 254.0, 200.0, 80.0 },
    { "axidraw_v3", 300.0, 218.0, 381.0, 250.0, 80.0 },
};

/**
 * @brief Пошук профілю пристрою за назвою моделі.
 * @param model Назва моделі.
 * @return Вказівник на профіль або NULL.
 */
static const axidraw_device_profile_t *profile_lookup (const char *model) {
    if (model && *model) {
        for (size_t i = 0;
             i < sizeof (k_axidraw_device_profiles) / sizeof (k_axidraw_device_profiles[0]); ++i) {
            if (string_equals_ci (model, k_axidraw_device_profiles[i].model))
                return &k_axidraw_device_profiles[i];
        }
    }
    return NULL;
}

/**
 * @brief Повертає типовий профіль пристрою за замовчуванням.
 * @return Вказівник на профіль.
 */
const axidraw_device_profile_t *axidraw_device_profile_default (void) {
    const axidraw_device_profile_t *profile = profile_lookup (CONFIG_DEFAULT_MODEL);
    if (profile)
        return profile;
    return &k_axidraw_device_profiles[0];
}

/**
 * @brief Повертає профіль для вказаної моделі або типовий.
 * @param model Назва моделі (напр., "minikit2").
 * @return Вказівник на профіль.
 */
const axidraw_device_profile_t *axidraw_device_profile_for_model (const char *model) {
    const axidraw_device_profile_t *profile = profile_lookup (model);
    if (profile)
        return profile;
    return axidraw_device_profile_default ();
}

/**
 * @brief Застосовує значення профілю до конфігурації.
 * @param cfg Конфігурація для оновлення.
 * @param profile Профіль або NULL, щоб взяти типовий.
 */
void axidraw_device_profile_apply (config_t *cfg, const axidraw_device_profile_t *profile) {
    if (!cfg)
        return;
    const axidraw_device_profile_t *effective
        = profile ? profile : axidraw_device_profile_default ();
    cfg->paper_w_mm = effective->paper_w_mm;
    cfg->paper_h_mm = effective->paper_h_mm;
    cfg->speed_mm_s = effective->speed_mm_s;
    cfg->accel_mm_s2 = effective->accel_mm_s2;
    log_print (
        LOG_DEBUG,
        "axidraw profile: застосовано %s (поле=%.1f×%.1f швидкість=%.1f прискорення=%.1f "
        "кроків/мм=%.2f)",
        effective->model, cfg->paper_w_mm, cfg->paper_h_mm, cfg->speed_mm_s, cfg->accel_mm_s2,
        effective->steps_per_mm);
}

/**
 * @brief Заповнює відсутні значення конфігурації даними з профілю.
 * @param cfg Конфігурація.
 * @param profile Профіль або NULL.
 * @return true — якщо щось змінено, false — без змін.
 */
bool axidraw_device_profile_backfill (config_t *cfg, const axidraw_device_profile_t *profile) {
    if (!cfg)
        return false;
    const axidraw_device_profile_t *effective
        = profile ? profile : axidraw_device_profile_default ();
    bool changed = false;
    if (!(cfg->paper_w_mm > 0.0)) {
        cfg->paper_w_mm = effective->paper_w_mm;
        changed = true;
    }
    if (!(cfg->paper_h_mm > 0.0)) {
        cfg->paper_h_mm = effective->paper_h_mm;
        changed = true;
    }
    if (!(cfg->speed_mm_s > 0.0)) {
        cfg->speed_mm_s = effective->speed_mm_s;
        changed = true;
    }
    if (!(cfg->accel_mm_s2 > 0.0)) {
        cfg->accel_mm_s2 = effective->accel_mm_s2;
        changed = true;
    }
    if (changed) {
        log_print (
            LOG_DEBUG,
            "axidraw profile: доповнено %s (поле=%.1f×%.1f швидкість=%.1f прискорення=%.1f "
            "кроків/мм=%.2f)",
            effective->model, cfg->paper_w_mm, cfg->paper_h_mm, cfg->speed_mm_s, cfg->accel_mm_s2,
            effective->steps_per_mm);
    }
    return changed;
}

/**
 * @brief Скидає лічильники/стан черги команд для сеансу.
 * @param dev Пристрій.
 */
static void axidraw_reset_runtime (axidraw_device_t *dev) {
    if (!dev)
        return;
    dev->last_cmd.tv_sec = 0;
    dev->last_cmd.tv_nsec = 0;
    dev->pending_commands = 0;
}

static void axidraw_sync_settings (axidraw_device_t *dev);
static int axidraw_require_connection (axidraw_device_t *dev);
static const char *axidraw_lock_path (void);

/**
 * @brief Обчислює шлях до lock-файлу у TMPDIR (кешується).
 * @return Статичний буфер зі шляхом.
 */
static const char *axidraw_lock_path (void) {
    static char path_buf[PATH_MAX];
    static bool initialized = false;
    if (!initialized) {
        const char *base = getenv ("TMPDIR");
        if (!base || !*base)
            base = "/tmp";
        size_t len = strlen (base);
        const char *suffix = "/cplot-axidraw.lock";
        if (len + strlen (suffix) + 1 >= sizeof (path_buf))
            snprintf (path_buf, sizeof (path_buf), "%s", "/tmp/cplot-axidraw.lock");
        else
            snprintf (
                path_buf, sizeof (path_buf), "%s%s%s", base,
                (len && base[len - 1] == '/') ? "" : "/", suffix);
        initialized = true;
    }
    return path_buf;
}

/**
 * @brief Скидає структуру налаштувань до безпечних типових значень.
 * @param settings Налаштування.
 */
void axidraw_settings_reset (axidraw_settings_t *settings) {
    if (!settings)
        return;
    memset (settings, 0, sizeof (*settings));
    settings->min_cmd_interval_ms = AXIDRAW_DEFAULT_MIN_INTERVAL_MS;
    settings->fifo_limit = AXIDRAW_DEFAULT_FIFO_LIMIT;
    settings->pen_up_delay_ms = 0;
    settings->pen_down_delay_ms = 0;
    settings->pen_up_pos = -1;
    settings->pen_down_pos = -1;
    settings->pen_up_speed = -1;
    settings->pen_down_speed = -1;
    settings->servo_timeout_s = -1;
    settings->speed_mm_s = 0.0;
    settings->accel_mm_s2 = 0.0;
    settings->steps_per_mm = 0.0;
}

/**
 * @brief Повертає поточні налаштування пристрою.
 * @param dev Пристрій.
 * @return Вказівник на налаштування або NULL.
 */
const axidraw_settings_t *axidraw_device_settings (const axidraw_device_t *dev) {
    if (!dev)
        return NULL;
    return &dev->settings;
}

/**
 * @brief Конвертує міліметри у кроки згідно з налаштуваннями.
 * @param dev Пристрій (для steps_per_mm).
 * @param mm Відстань у мм.
 * @return Кроки (із насиченням до int32_t).
 */
static int32_t mm_to_steps (const axidraw_device_t *dev, double mm) {
    const axidraw_settings_t *cfg = axidraw_device_settings (dev);
    double spmm = (cfg && cfg->steps_per_mm > 0.0) ? cfg->steps_per_mm : 0.0;
    if (!(spmm > 0.0)) {
        LOGE (AXIDRAW_LOG ("Коефіцієнт кроків на мм не встановлено — профіль не застосовано"));
        return 0;
    }
    if (!(mm > -1e300 && mm < 1e300)) {
        LOGW (AXIDRAW_LOG ("Некоректна відстань мм=%.2f — повертаю 0"), mm);
        return 0;
    }
    double scaled = llround (mm * spmm);
    if (scaled > (double)INT32_MAX)
        return INT32_MAX;
    if (scaled < (double)INT32_MIN)
        return INT32_MIN;
    return (int32_t)scaled;
}

/**
 * @brief Обчислює тривалість руху з урахуванням швидкості.
 * @param distance_mm Відстань у мм.
 * @param speed_mm_s Швидкість у мм/с.
 * @return Тривалість у мс (мінімум 1, обмежено).
 */
static uint32_t duration_from_mm_speed (double distance_mm, double speed_mm_s) {
    if (!(distance_mm > 0.0) || !(speed_mm_s > 0.0))
        return 1u;
    double ms = ceil ((distance_mm / speed_mm_s) * 1000.0);
    if (ms < 1.0)
        ms = 1.0;
    if (ms > (double)AXIDRAW_MAX_DURATION_MS)
        ms = (double)AXIDRAW_MAX_DURATION_MS;
    return (uint32_t)ms;
}

/**
 * @brief Рух у міліметрах із заданою швидкістю.
 * @param dev Пристрій.
 * @param dx_mm Зсув по X (мм).
 * @param dy_mm Зсув по Y (мм).
 * @param speed_mm_s Швидкість (мм/с), 0 — використати налаштування.
 * @return 0 — успіх, -1 — помилка.
 */
int axidraw_move_mm (axidraw_device_t *dev, double dx_mm, double dy_mm, double speed_mm_s) {
    if (!dev)
        return -1;
    const axidraw_settings_t *cfg = axidraw_device_settings (dev);
    double speed = (speed_mm_s > 0.0) ? speed_mm_s
                                      : ((cfg && cfg->speed_mm_s > 0.0) ? cfg->speed_mm_s : 75.0);
    int32_t steps_x = mm_to_steps (dev, dx_mm);
    int32_t steps_y = mm_to_steps (dev, dy_mm);
    double distance = hypot (dx_mm, dy_mm);
    uint32_t duration = duration_from_mm_speed (distance, speed);
    return axidraw_move_xy (dev, duration, steps_x, steps_y);
}

/**
 * @brief Застосовує налаштування до пристрою і синхронізує з контролером (якщо підключено).
 * @param dev Пристрій.
 * @param settings Джерело налаштувань.
 */
void axidraw_apply_settings (axidraw_device_t *dev, const axidraw_settings_t *settings) {
    if (!dev || !settings)
        return;
    dev->settings = *settings;
    axidraw_set_rate_limit (dev, dev->settings.min_cmd_interval_ms);
    axidraw_set_fifo_limit (dev, dev->settings.fifo_limit);
    if (axidraw_device_is_connected (dev))
        axidraw_sync_settings (dev);
}

/**
 * @brief Ініціалізує структуру стану пристрою до типових значень.
 * @param dev Пристрій.
 */
void axidraw_device_init (axidraw_device_t *dev) {
    if (!dev)
        return;
    memset (dev, 0, sizeof (*dev));
    dev->baud = 9600;
    dev->timeout_ms = 5000;
    axidraw_settings_reset (&dev->settings);
    dev->min_cmd_interval = dev->settings.min_cmd_interval_ms;
    dev->max_fifo_commands = dev->settings.fifo_limit;
    axidraw_reset_runtime (dev);
    LOGD (
        AXIDRAW_LOG ("Ініціалізація (baud=%d timeout=%d min_interval=%.1f)"), dev->baud,
        dev->timeout_ms, dev->min_cmd_interval);
}

/**
 * @brief Налаштовує параметри доступу до серійного порту та інтервал команд.
 * @param dev Пристрій.
 * @param port_path Шлях до tty-порту (може бути NULL для збереження попереднього).
 * @param baud Бодова швидкість (9600 тощо).
 * @param timeout_ms Тайм-аут читання (мс).
 * @param min_cmd_interval_ms Мінімальний інтервал між командами (мс, >=0).
 */
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
    LOGD (
        AXIDRAW_LOG ("Конфігурація port=%s baud=%d timeout=%d min_interval=%.1f"), dev->port_path,
        dev->baud, dev->timeout_ms, dev->min_cmd_interval);
    log_print (
        LOG_INFO, "налаштування пристрою: порт=%s бод=%d тайм-аут=%d мін_інтервал=%.1f",
        dev->port_path[0] ? dev->port_path : "<невказано>", dev->baud, dev->timeout_ms,
        dev->min_cmd_interval);
}

/**
 * @brief Евристичний пошук порту AxiDraw (переважно macOS).
 * @param dev Пристрій.
 * @param buf [out] Буфер для шляху.
 * @param len Розмір буфера.
 * @return 0 — знайдено, інакше помилка.
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
        LOGI (AXIDRAW_LOG ("Автоматично знайдено порт %s"), dev->port_path);
        log_print (LOG_INFO, "пристрій: автоматично знайдено порт %s", dev->port_path);
        return 0;
    }
#endif
    LOGW (AXIDRAW_LOG ("Не вдалося автоматично знайти порт"));
    log_print (LOG_WARN, "пристрій: автоматичний пошук порту не дав результату");
    return -1;
}

/**
 * @brief Встановлює з'єднання з контролером EBB і перевіряє відповідь.
 * @param dev Пристрій.
 * @param errbuf [out] Буфер для тексту помилки (може бути NULL).
 * @param errlen Довжина буфера.
 * @return 0 — успіх, -1 — помилка.
 */
int axidraw_device_connect (axidraw_device_t *dev, char *errbuf, size_t errlen) {
    if (!dev)
        return -1;
    if (dev->connected)
        return 0;

    char guess_buf[256] = { 0 };
    if (dev->port_path[0] == '\0') {
        if (axidraw_guess_port (dev, guess_buf, sizeof (guess_buf)) != 0) {
            if (errbuf && errlen > 0)
                snprintf (errbuf, errlen, "%s", AXIDRAW_ERR_PORT_NOT_SPECIFIED);
            log_print (LOG_WARN, "axidraw: порт не вказано, автопошук не знайшов пристрій");
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
        log_print (
            LOG_ERROR, "axidraw: помилка відкриття порту %s (%s)",
            dev->port_path[0] ? dev->port_path : "<невідомий>", err_dst);
        return -1;
    }
    serial_flush_input (sp);
    if (serial_probe_ebb (sp, NULL, 0) != 0) {
        if (!errbuf)
            LOGE (AXIDRAW_LOG ("Пристрій не відповідає на команду V"));
        serial_close (sp);
        log_print (LOG_ERROR, "axidraw: пристрій не відповідає на команду V");
        return -1;
    }
    dev->port = sp;
    dev->connected = true;
    axidraw_reset_runtime (dev);
    axidraw_sync_settings (dev);
    LOGI (AXIDRAW_LOG ("Підключено через %s"), dev->port_path);
    log_print (LOG_INFO, "axidraw: підключено %s @%d бод", dev->port_path, dev->baud);
    return 0;
}

/**
 * @brief Розриває з'єднання з контролером і скидає стан.
 * @param dev Пристрій.
 */
void axidraw_device_disconnect (axidraw_device_t *dev) {
    if (!dev)
        return;
    if (dev->port) {
        serial_close (dev->port);
        dev->port = NULL;
        LOGI (AXIDRAW_LOG ("Відключено від %s"), dev->port_path);
        log_print (LOG_INFO, "axidraw: відключено %s", dev->port_path);
    }
    dev->connected = false;
    axidraw_reset_runtime (dev);
}

/**
 * @brief Захоплює lock-файл для ексклюзивного доступу до пристрою.
 * @param out_fd [out] Дескриптор lock-файлу.
 * @return 0 — успіх, -1 — помилка.
 */
int axidraw_device_lock_acquire (int *out_fd) {
    if (!out_fd)
        return -1;
    const char *lock_path = axidraw_lock_path ();
    int fd = open (lock_path, O_RDWR | O_CREAT, 0600);
    if (fd < 0) {
        log_print (LOG_ERROR, "axidraw lock: не вдалося відкрити %s", lock_path);
        return -1;
    }
    if (flock (fd, LOCK_EX | LOCK_NB) != 0) {
        log_print (LOG_WARN, "axidraw lock: ресурс зайнятий %s", lock_path);
        close (fd);
        return -1;
    }
    if (ftruncate (fd, 0) == 0) {
        char buf[64];
        int len = snprintf (buf, sizeof (buf), "pid=%ld\n", (long)getpid ());
        if (len > 0)
            write (fd, buf, (size_t)len);
    }
    *out_fd = fd;
    log_print (LOG_INFO, "axidraw lock: отримано %s fd=%d", lock_path, fd);
    return 0;
}

/**
 * @brief Звільняє lock-файл AxiDraw.
 * @param fd Дескриптор, отриманий під час захоплення.
 */
void axidraw_device_lock_release (int fd) {
    if (fd < 0)
        return;
    flock (fd, LOCK_UN);
    close (fd);
    log_print (LOG_INFO, "axidraw lock: звільнено fd=%d", fd);
}

/**
 * @brief Повертає шлях до lock-файлу.
 * @return C-рядок зі шляхом.
 */
const char *axidraw_device_lock_file (void) { return axidraw_lock_path (); }

/**
 * @brief Виконує аварійну зупинку (наприклад, ES-команду) і скидає стан черги.
 * @param dev Пристрій.
 * @return 0 — успіх, -1 — помилка.
 */
int axidraw_emergency_stop (axidraw_device_t *dev) {
    if (axidraw_require_connection (dev) != 0)
        return -1;
    int rc = ebb_emergency_stop (dev->port, dev->timeout_ms);
    if (rc == 0) {
        axidraw_reset_runtime (dev);
        log_print (LOG_WARN, "axidraw: аварійна зупинка виконана");
    } else {
        log_print (LOG_ERROR, "axidraw: аварійна зупинка повернула %d", rc);
    }
    return rc;
}

/**
 * @brief Перевіряє факт активного підключення до пристрою.
 * @param dev Пристрій.
 * @return true — підключено, false — ні.
 */
bool axidraw_device_is_connected (const axidraw_device_t *dev) { return dev && dev->connected; }

/**
 * @brief Встановлює мінімальний інтервал між командами (мс).
 * @param dev Пристрій.
 * @param min_interval_ms Мінімальний інтервал у мс.
 */
void axidraw_set_rate_limit (axidraw_device_t *dev, double min_interval_ms) {
    if (!dev)
        return;
    dev->min_cmd_interval = min_interval_ms >= 0.0 ? min_interval_ms : 0.0;
    dev->settings.min_cmd_interval_ms = dev->min_cmd_interval;
    LOGD (AXIDRAW_LOG ("Мінімальний інтервал між командами %.2f мс"), dev->min_cmd_interval);
    log_print (LOG_INFO, "частота команд: обмеження %.2f мс", dev->min_cmd_interval);
}

/**
 * @brief Встановлює ліміт команд у FIFO (0 — без обмеження).
 * @param dev Пристрій.
 * @param max_fifo_commands Максимальна кількість активних+чергових команд.
 */
void axidraw_set_fifo_limit (axidraw_device_t *dev, size_t max_fifo_commands) {
    if (!dev)
        return;
    dev->max_fifo_commands = max_fifo_commands;
    dev->settings.fifo_limit = max_fifo_commands;
    if (max_fifo_commands == 0) {
        LOGD (AXIDRAW_LOG ("Ліміт FIFO команд вимкнено"));
        log_print (LOG_INFO, "черга: ліміт вимкнено");
    } else {
        LOGD (AXIDRAW_LOG ("Ліміт FIFO команд %zu"), dev->max_fifo_commands);
        log_print (LOG_INFO, "черга: ліміт %zu", dev->max_fifo_commands);
    }
}

/**
 * @brief Перетворює відсоток (0–100) у імпульс серво (ticks).
 * @param percent Значення у відсотках.
 * @return Значення 1–65535.
 */
static int axidraw_percent_to_servo (int percent) {
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;
    long range = (long)AXIDRAW_SERVO_MAX - (long)AXIDRAW_SERVO_MIN;
    long value = (long)AXIDRAW_SERVO_MIN + ((range * percent + 50L) / 100L);
    if (value < 1)
        value = 1;
    if (value > 65535)
        value = 65535;
    return (int)value;
}

/**
 * @brief Перетворює швидкість у відсотках у rate для сервоприводу.
 * @param speed_percent 0–100.
 * @return Rate 0–65535.
 */
static int axidraw_speed_to_rate (int speed_percent) {
    if (speed_percent < 0)
        speed_percent = 0;
    long value = (long)speed_percent * AXIDRAW_SERVO_SPEED_SCALE;
    if (value > 65535)
        value = 65535;
    return (int)value;
}

/**
 * @brief Перевіряє, що пристрій підключено та має порт.
 * @param dev Пристрій.
 * @return 0 — ок, -1 — немає зʼєднання.
 */
static int axidraw_require_connection (axidraw_device_t *dev) {
    if (!dev || !dev->connected || !dev->port) {
        LOGE (AXIDRAW_LOG ("Пристрій не підключено"));
        log_print (LOG_ERROR, "axidraw: пристрій не підключено");
        return -1;
    }
    return 0;
}

/**
 * @brief Оновлює внутрішню оцінку зайнятості FIFO команд.
 * @param dev Пристрій.
 * @return 0 — успіх, -1 — помилка.
 */
static int axidraw_refresh_queue (axidraw_device_t *dev) {
    if (!dev)
        return -1;
    if (dev->max_fifo_commands == 0)
        return 0;
    if (!dev->port)
        return -1;

    ebb_motion_status_t status;
    if (ebb_query_motion (dev->port, &status, dev->timeout_ms) != 0)
        return -1;

    size_t queued = status.fifo_pending > 0 ? (size_t)status.fifo_pending : 0u;
    size_t active = status.command_active ? 1u : 0u;
    dev->pending_commands = queued + active;
    log_print (
        LOG_DEBUG, "черга: активні=%zu у_черзі=%zu (разом %zu)", active, queued,
        dev->pending_commands);
    return 0;
}

/**
 * @brief Очікує, доки зʼявиться місце у черзі команд.
 * @param dev Пристрій.
 * @return 0 — слот доступний, -1 — помилка/тайм-аут.
 */
static int axidraw_wait_queue_slot (axidraw_device_t *dev) {
    if (!dev)
        return -1;
    if (dev->max_fifo_commands == 0)
        return 0;
    if (dev->pending_commands < dev->max_fifo_commands)
        return 0;

    log_print (
        LOG_DEBUG, "черга: очікування місця (%zu/%zu)", dev->pending_commands,
        dev->max_fifo_commands);

    if (axidraw_refresh_queue (dev) != 0)
        return -1;
    if (dev->pending_commands < dev->max_fifo_commands)
        return 0;

    struct timespec start;
    bool have_clock = clock_gettime (CLOCK_MONOTONIC, &start) == 0;
    struct timespec sleep_ts = { .tv_sec = 0, .tv_nsec = 5 * 1000000L };
    bool logged = false;

    while (dev->pending_commands >= dev->max_fifo_commands) {
        if (!logged) {
            LOGD (
                "axidraw: очікування FIFO (%zu/%zu)", dev->pending_commands,
                dev->max_fifo_commands);
            logged = true;
            log_print (
                LOG_DEBUG, "черга: ще зайнято (%zu/%zu)", dev->pending_commands,
                dev->max_fifo_commands);
        }
        nanosleep (&sleep_ts, NULL);
        if (axidraw_refresh_queue (dev) != 0)
            return -1;
        if (!have_clock)
            continue;
        struct timespec now;
        if (clock_gettime (CLOCK_MONOTONIC, &now) != 0) {
            have_clock = false;
            continue;
        }
        double waited = time_diff_ms (&now, &start);
        if ((int)waited >= dev->timeout_ms) {
            LOGE (AXIDRAW_LOG ("Перевищено тайм-аут очікування FIFO"));
            log_print (LOG_ERROR, "черга: тайм-аут очікування (>%d мс)", dev->timeout_ms);
            return -1;
        }
    }

    log_print (
        LOG_DEBUG, "черга: місце отримано (%zu/%zu)", dev->pending_commands,
        dev->max_fifo_commands);

    return 0;
}

/**
 * @brief Гарантує мінімальний інтервал між відправками команд.
 * @param dev Пристрій.
 * @return 0 — ок, -1 — помилка часу.
 */
static int axidraw_wait_interval (axidraw_device_t *dev) {
    if (!dev)
        return -1;
    if (dev->min_cmd_interval <= 0.0)
        return 0;
    if (dev->last_cmd.tv_sec == 0 && dev->last_cmd.tv_nsec == 0)
        return 0;

    struct timespec now;
    bool logged = false;
    while (1) {
        if (clock_gettime (CLOCK_MONOTONIC, &now) != 0)
            return 0;
        double elapsed = time_diff_ms (&now, &dev->last_cmd);
        if (elapsed >= dev->min_cmd_interval)
            return 0;
        double remaining = dev->min_cmd_interval - elapsed;
        if (remaining <= 0.0)
            return 0;
        long ms_whole = (long)remaining;
        long ns_part = (long)((remaining - (double)ms_whole) * 1e6);
        struct timespec ts
            = { .tv_sec = ms_whole / 1000, .tv_nsec = (ms_whole % 1000) * 1000000L + ns_part };
        if (!logged) {
            LOGD (AXIDRAW_LOG ("Затримка %.2f мс перед наступною командою"), remaining);
            logged = true;
            log_print (LOG_DEBUG, "частота: очікування %.2f мс", remaining);
        }
        nanosleep (&ts, NULL);
    }
}

/**
 * @brief Композитне очікування: FIFO слот + інтервал.
 * @param dev Пристрій.
 * @return 0 — можна надсилати команду, -1 — помилка.
 */
static int axidraw_wait_slot (axidraw_device_t *dev) {
    if (axidraw_wait_queue_slot (dev) != 0)
        return -1;
    if (axidraw_wait_interval (dev) != 0)
        return -1;
    log_print (LOG_DEBUG, "axidraw: слот доступний для наступної команди");
    return 0;
}

/**
 * @brief Позначає, що команда відправлена (оновлює last_cmd/FIFO).
 * @param dev Пристрій.
 */
static void axidraw_mark_dispatched (axidraw_device_t *dev) {
    if (!dev)
        return;
    struct timespec now;
    if (clock_gettime (CLOCK_MONOTONIC, &now) == 0)
        dev->last_cmd = now;
    if (dev->pending_commands < SIZE_MAX)
        ++dev->pending_commands;
    log_print (LOG_DEBUG, "черга: відправлено, у черзі %zu", dev->pending_commands);
}

/**
 * @brief Синхронізує налаштування AxiDraw з контролером EBB.
 * @param dev Пристрій.
 */
static void axidraw_sync_settings (axidraw_device_t *dev) {
    if (!dev)
        return;
    if (axidraw_require_connection (dev) != 0)
        return;

    const axidraw_settings_t *cfg = &dev->settings;

    log_print (LOG_DEBUG, "налаштування: синхронізація з пристроєм");

    if (ebb_configure_mode (dev->port, 1, 1, dev->timeout_ms) != 0) {
        LOGW (AXIDRAW_LOG ("Не вдалося активувати сервопривід (SC,1,1)"));
        log_print (LOG_WARN, "налаштування: SC,1,1 не виконано");
    }

    if (cfg->pen_up_pos >= 0) {
        int up = axidraw_percent_to_servo (cfg->pen_up_pos);
        if (ebb_configure_mode (dev->port, 4, up, dev->timeout_ms) != 0) {
            LOGW (AXIDRAW_LOG ("Не вдалося налаштувати позицію пера вгору (SC,4,%d)"), up);
            log_print (LOG_WARN, "налаштування: SC,4,%d відхилено", up);
        } else {
            LOGD (AXIDRAW_LOG ("Позиція пера вгору %d%% → %d"), cfg->pen_up_pos, up);
            log_print (LOG_DEBUG, "налаштування: SC,4,%d (%.0f%%)", up, (double)cfg->pen_up_pos);
        }
    }

    if (cfg->pen_down_pos >= 0) {
        int down = axidraw_percent_to_servo (cfg->pen_down_pos);
        if (ebb_configure_mode (dev->port, 5, down, dev->timeout_ms) != 0) {
            LOGW (AXIDRAW_LOG ("Не вдалося налаштувати позицію пера вниз (SC,5,%d)"), down);
            log_print (LOG_WARN, "налаштування: SC,5,%d відхилено", down);
        } else {
            LOGD (AXIDRAW_LOG ("Позиція пера вниз %d%% → %d"), cfg->pen_down_pos, down);
            log_print (
                LOG_DEBUG, "налаштування: SC,5,%d (%.0f%%)", down, (double)cfg->pen_down_pos);
        }
    }

    if (cfg->pen_up_speed >= 0) {
        int up_speed = axidraw_speed_to_rate (cfg->pen_up_speed);
        if (ebb_configure_mode (dev->port, 11, up_speed, dev->timeout_ms) != 0) {
            LOGW (
                AXIDRAW_LOG ("Не вдалося налаштувати швидкість підйому пера (SC,11,%d)"), up_speed);
            log_print (LOG_WARN, "налаштування: SC,11,%d відхилено", up_speed);
        } else {
            log_print (LOG_DEBUG, "налаштування: SC,11,%d", up_speed);
        }
    }

    if (cfg->pen_down_speed >= 0) {
        int down_speed = axidraw_speed_to_rate (cfg->pen_down_speed);
        if (ebb_configure_mode (dev->port, 12, down_speed, dev->timeout_ms) != 0) {
            LOGW (
                AXIDRAW_LOG ("Не вдалося налаштувати швидкість опускання пера (SC,12,%d)"),
                down_speed);
            log_print (LOG_WARN, "налаштування: SC,12,%d відхилено", down_speed);
        } else {
            log_print (LOG_DEBUG, "налаштування: SC,12,%d", down_speed);
        }
    }

    if (cfg->servo_timeout_s >= 0) {
        uint64_t timeout_ms = (uint64_t)cfg->servo_timeout_s * 1000ULL;
        if (timeout_ms > UINT32_MAX)
            timeout_ms = UINT32_MAX;
        if (ebb_set_servo_power_timeout (dev->port, (uint32_t)timeout_ms, 1, dev->timeout_ms)
            != 0) {
            LOGW (
                AXIDRAW_LOG ("Не вдалося налаштувати тайм-аут сервоприводу (SR,%llu)"),
                (unsigned long long)timeout_ms);
            log_print (LOG_WARN, "налаштування: SR,%llu відхилено", (unsigned long long)timeout_ms);
        } else {
            log_print (LOG_DEBUG, "налаштування: SR,%llu", (unsigned long long)timeout_ms);
        }
    }
}

/**
 * @brief Виконує команду підняття/опускання пера з урахуванням затримки.
 * @param dev Пристрій.
 * @param pen_up true — підняти, false — опустити.
 * @return 0 — успіх, -1 — помилка.
 */
static int axidraw_exec_pen (axidraw_device_t *dev, bool pen_up) {
    if (axidraw_require_connection (dev) != 0)
        return -1;
    if (axidraw_wait_slot (dev) != 0)
        return -1;
    int delay_ms = pen_up ? dev->settings.pen_up_delay_ms : dev->settings.pen_down_delay_ms;
    if (delay_ms < 0)
        delay_ms = 0;
    LOGD (AXIDRAW_LOG ("Перо %s (затримка %d мс)"), pen_up ? "вгору" : "вниз", delay_ms);
    log_print (LOG_DEBUG, "axidraw pen: %s delay=%d", pen_up ? "up" : "down", delay_ms);
    int rc = ebb_pen_set (dev->port, pen_up, delay_ms, -1, dev->timeout_ms);
    if (rc == 0) {
        axidraw_mark_dispatched (dev);
        log_print (LOG_DEBUG, "axidraw pen: команда успішна");
    } else {
        LOGE (AXIDRAW_LOG ("Команда пера повернула помилку (%d)"), rc);
        log_print (LOG_ERROR, "axidraw pen: помилка %d", rc);
    }
    return rc;
}

/**
 * @brief Піднімає перо (викликає EBB через pen_set).
 * @param dev Пристрій.
 * @return 0 — успіх, -1 — помилка.
 */
int axidraw_pen_up (axidraw_device_t *dev) { return axidraw_exec_pen (dev, true); }

/**
 * @brief Опускає перо (викликає EBB через pen_set).
 * @param dev Пристрій.
 * @return 0 — успіх, -1 — помилка.
 */
int axidraw_pen_down (axidraw_device_t *dev) { return axidraw_exec_pen (dev, false); }

/**
 * @brief Виконує одну SM-команду руху з тайм-аутом та FIFO-логікою.
 * @param dev Пристрій.
 * @param fn Функція надсилання (EBB).
 * @param duration Тривалість у мс.
 * @param a Параметр A/Step1.
 * @param b Параметр B/Step2.
 * @return 0 — успіх, -1 — помилка.
 */
static int axidraw_exec_sm (
    axidraw_device_t *dev,
    int (*fn) (serial_port_t *, uint32_t, int32_t, int32_t, int),
    uint32_t duration,
    int32_t a,
    int32_t b) {
    if (axidraw_require_connection (dev) != 0)
        return -1;
    if (axidraw_wait_slot (dev) != 0)
        return -1;
    LOGD (AXIDRAW_LOG ("Рух duration=%u a=%d b=%d"), duration, a, b);
    log_print (LOG_DEBUG, "axidraw SM: duration=%u a=%d b=%d", duration, a, b);
    int rc = fn (dev->port, duration, a, b, dev->timeout_ms);
    if (rc == 0) {
        axidraw_mark_dispatched (dev);
        log_print (LOG_DEBUG, "axidraw SM: команда успішна");
    } else {
        LOGE (AXIDRAW_LOG ("Команда руху повернула помилку (%d)"), rc);
        log_print (LOG_ERROR, "axidraw SM: помилка %d", rc);
    }
    return rc;
}

/**
 * @brief Рух по осях X/Y на задану кількість кроків за визначений час.
 * @param dev Пристрій.
 * @param duration_ms Тривалість інтервалу (мс).
 * @param steps_x Кроки по X.
 * @param steps_y Кроки по Y.
 * @return 0 — успіх, -1 — помилка.
 */
int axidraw_move_xy (
    axidraw_device_t *dev, uint32_t duration_ms, int32_t steps_x, int32_t steps_y) {
    return axidraw_exec_sm (dev, ebb_move_steps, duration_ms, steps_x, steps_y);
}

/**
 * @brief Рух у кінематиці CoreXY на задані кроки A/B.
 * @param dev Пристрій.
 * @param duration_ms Тривалість (мс).
 * @param steps_a Кроки вздовж A.
 * @param steps_b Кроки вздовж B.
 * @return 0 — успіх, -1 — помилка.
 */
int axidraw_move_corexy (
    axidraw_device_t *dev, uint32_t duration_ms, int32_t steps_a, int32_t steps_b) {
    if (axidraw_require_connection (dev) != 0)
        return -1;
    if (axidraw_wait_slot (dev) != 0)
        return -1;
    log_print (LOG_DEBUG, "axidraw XM: duration=%u a=%d b=%d", duration_ms, steps_a, steps_b);
    int rc = ebb_move_mixed (dev->port, duration_ms, steps_a, steps_b, dev->timeout_ms);
    if (rc == 0) {
        axidraw_mark_dispatched (dev);
        log_print (LOG_DEBUG, "axidraw XM: команда успішна");
    } else {
        LOGE (AXIDRAW_LOG ("Команда XM повернула помилку (%d)"), rc);
        log_print (LOG_ERROR, "axidraw XM: помилка %d", rc);
    }
    return rc;
}

/**
 * @brief Низькорівнева команда руху з параметрами швидкості/прискорення для обох осей.
 * @param dev Пристрій.
 * @param rate1 Швидкість осі 1.
 * @param steps1 Кроки осі 1.
 * @param accel1 Прискорення осі 1.
 * @param rate2 Швидкість осі 2.
 * @param steps2 Кроки осі 2.
 * @param accel2 Прискорення осі 2.
 * @param clear_flags Прапори очищення (FIFO/лічильники).
 * @return 0 — успіх, -1 — помилка.
 */
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
    if (axidraw_wait_slot (dev) != 0)
        return -1;
    LOGD (AXIDRAW_LOG ("LM rate1=%u steps1=%d rate2=%u steps2=%d"), rate1, steps1, rate2, steps2);
    log_print (
        LOG_DEBUG, "axidraw LM: rate1=%u steps1=%d accel1=%d rate2=%u steps2=%d accel2=%d flags=%d",
        rate1, steps1, accel1, rate2, steps2, accel2, clear_flags);
    int rc = ebb_move_lowlevel_steps (
        dev->port, rate1, steps1, accel1, rate2, steps2, accel2, clear_flags, dev->timeout_ms);
    if (rc == 0) {
        axidraw_mark_dispatched (dev);
        log_print (LOG_DEBUG, "axidraw LM: команда успішна");
    } else {
        LOGE (AXIDRAW_LOG ("Команда LM повернула помилку (%d)"), rc);
        log_print (LOG_ERROR, "axidraw LM: помилка %d", rc);
    }
    return rc;
}

/**
 * @brief Низькорівневий рух з фіксованою кількістю інтервалів.
 * @param dev Пристрій.
 * @param intervals К-сть інтервалів.
 * @param rate1 Швидкість осі 1.
 * @param accel1 Прискорення осі 1.
 * @param rate2 Швидкість осі 2.
 * @param accel2 Прискорення осі 2.
 * @param clear_flags Прапори очищення.
 * @return 0 — успіх, -1 — помилка.
 */
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
    if (axidraw_wait_slot (dev) != 0)
        return -1;
    LOGD (AXIDRAW_LOG ("LT intervals=%u rate1=%d rate2=%d"), intervals, rate1, rate2);
    log_print (
        LOG_DEBUG, "axidraw LT: intervals=%u rate1=%d accel1=%d rate2=%d accel2=%d flags=%d",
        intervals, rate1, accel1, rate2, accel2, clear_flags);
    int rc = ebb_move_lowlevel_time (
        dev->port, intervals, rate1, accel1, rate2, accel2, clear_flags, dev->timeout_ms);
    if (rc == 0) {
        axidraw_mark_dispatched (dev);
        log_print (LOG_DEBUG, "axidraw LT: команда успішна");
    } else {
        LOGE (AXIDRAW_LOG ("Команда LT повернула помилку (%d)"), rc);
        log_print (LOG_ERROR, "axidraw LT: помилка %d", rc);
    }
    return rc;
}

/**
 * @brief Повернення у "home" позиції з заданою швидкістю кроків.
 * @param dev Пристрій.
 * @param step_rate Частота кроків.
 * @param pos1 Ціль для осі 1 (може бути NULL).
 * @param pos2 Ціль для осі 2 (може бути NULL).
 * @return 0 — успіх, -1 — помилка.
 */
int axidraw_home (
    axidraw_device_t *dev, uint32_t step_rate, const int32_t *pos1, const int32_t *pos2) {
    if (axidraw_require_connection (dev) != 0)
        return -1;
    if (axidraw_wait_slot (dev) != 0)
        return -1;
    LOGD (
        "пристрій: HM швидкість_кроків=%u pos1=%d pos2=%d", step_rate, pos1 ? *pos1 : 0,
        pos2 ? *pos2 : 0);
    log_print (
        LOG_DEBUG, "пристрій HM: швидкість_кроків=%u pos1=%d pos2=%d", step_rate, pos1 ? *pos1 : 0,
        pos2 ? *pos2 : 0);
    int rc = ebb_home_move (dev->port, step_rate, pos1, pos2, dev->timeout_ms);
    if (rc == 0) {
        axidraw_mark_dispatched (dev);
        log_print (LOG_DEBUG, "пристрій HM: команда успішна");
    } else {
        LOGE (AXIDRAW_LOG ("Команда HM повернула помилку (%d)"), rc);
        log_print (LOG_ERROR, "пристрій HM: помилка %d", rc);
    }
    return rc;
}
