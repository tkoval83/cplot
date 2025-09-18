/**
 * @file axidraw.c
 * @brief Реалізація високорівневого менеджера AxiDraw.
 */

#include "axidraw.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>

#define AXIDRAW_DEFAULT_FIFO_LIMIT 3
#define AXIDRAW_DEFAULT_MIN_INTERVAL_MS 5.0
#define AXIDRAW_SERVO_MIN 7500
#define AXIDRAW_SERVO_MAX 28000
#define AXIDRAW_SERVO_SPEED_SCALE 5

#include "log.h"
#include "trace.h"

#ifdef DEBUG
#define AXIDRAW_LOG(MSG) "axidraw: " MSG
#else
#define AXIDRAW_LOG(MSG) MSG
#endif

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
 * @brief Скинути оперативний стан (таймери та лічильники) після підключення.
 *
 * @param dev Структура пристрою.
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
 * @brief Побудувати шлях до lock-файла для взаємного виключення AxiDraw.
 *
 * Використовує TMPDIR, якщо змінна задана, інакше падає назад на /tmp. Значення
 * кешується у статичному буфері, оскільки воно не змінюється протягом життя процесу.
 *
 * @return Нуль-термінований рядок із шляхом lock-файла.
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
                path_buf, sizeof (path_buf), "%s%s%s", base, (len && base[len - 1] == '/') ? "" : "/",
                suffix);
        initialized = true;
    }
    return path_buf;
}

/**
 * @brief Скинути структуру налаштувань до типового стану.
 *
 * @param settings Структура налаштувань для ініціалізації (не NULL).
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
}

/**
 * @brief Отримати останні застосовані налаштування.
 *
 * @param dev Пристрій AxiDraw.
 * @return Вказівник на внутрішню структуру налаштувань або NULL.
 */
const axidraw_settings_t *axidraw_device_settings (const axidraw_device_t *dev) {
    if (!dev)
        return NULL;
    return &dev->settings;
}

/**
 * @brief Застосувати набір налаштувань на стороні хоста (rate/FIFO).
 *
 * Якщо пристрій уже підключено, додатково оновлює параметри через SC/SR-команди.
 *
 * @param dev      Пристрій.
 * @param settings Нові налаштування (не NULL).
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

/** Ініціалізувати структуру dev типовими значеннями. */
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
        AXIDRAW_LOG("Ініціалізація (baud=%d timeout=%d min_interval=%.1f)"), dev->baud,
        dev->timeout_ms, dev->min_cmd_interval);
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
    LOGD (
        AXIDRAW_LOG("Конфігурація port=%s baud=%d timeout=%d min_interval=%.1f"), dev->port_path,
        dev->baud, dev->timeout_ms, dev->min_cmd_interval);
    trace_write (
        LOG_INFO,
        "axidraw config: port=%s baud=%d timeout=%d min_interval=%.1f",
        dev->port_path[0] ? dev->port_path : "<невказано>", dev->baud, dev->timeout_ms,
        dev->min_cmd_interval);
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
        LOGI (AXIDRAW_LOG("Автоматично знайдено порт %s"), dev->port_path);
        trace_write (LOG_INFO, "axidraw: автоматично знайдено порт %s", dev->port_path);
        return 0;
    }
#endif
    LOGW (AXIDRAW_LOG("Не вдалося автоматично знайти порт"));
    trace_write (LOG_WARN, "axidraw: автоматичний пошук порту не дав результату");
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
            if (errbuf && errlen > 0)
                snprintf (errbuf, errlen, "%s", AXIDRAW_ERR_PORT_NOT_SPECIFIED);
            trace_write (LOG_WARN, "axidraw: порт не вказано, автопошук не знайшов пристрій");
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
        trace_write (
            LOG_ERROR, "axidraw: помилка відкриття порту %s (%s)",
            dev->port_path[0] ? dev->port_path : "<невідомий>", err_dst);
        return -1;
    }
    serial_flush_input (sp);
    if (serial_probe_ebb (sp, NULL, 0) != 0) {
        if (!errbuf)
            LOGE (AXIDRAW_LOG("Пристрій не відповідає на команду V"));
        serial_close (sp);
        trace_write (LOG_ERROR, "axidraw: пристрій не відповідає на команду V");
        return -1;
    }
    dev->port = sp;
    dev->connected = true;
    axidraw_reset_runtime (dev);
    axidraw_sync_settings (dev);
    LOGI (AXIDRAW_LOG("Підключено через %s"), dev->port_path);
    trace_write (LOG_INFO, "axidraw: підключено %s @%d бод", dev->port_path, dev->baud);
    return 0;
}

/** Закрити порт та скинути статус. */
void axidraw_device_disconnect (axidraw_device_t *dev) {
    if (!dev)
        return;
    if (dev->port) {
        serial_close (dev->port);
        dev->port = NULL;
        LOGI (AXIDRAW_LOG("Відключено від %s"), dev->port_path);
        trace_write (LOG_INFO, "axidraw: відключено %s", dev->port_path);
    }
    dev->connected = false;
    axidraw_reset_runtime (dev);
}

/**
 * @brief Отримати взаємне виключення доступу до пристрою.
 *
 * Створює (за потреби) lock-файл і робить неблокуючий LOCK_EX. У разі успіху
 * поверх файлу записується PID поточного процесу для діагностики.
 *
 * @param[out] out_fd Дескриптор lock-файла (для подальшого release).
 * @return 0 — успіх, -1 — ресурс зайнято або сталася помилка I/O.
 */
int axidraw_device_lock_acquire (int *out_fd) {
    if (!out_fd)
        return -1;
    const char *lock_path = axidraw_lock_path ();
    int fd = open (lock_path, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        trace_write (LOG_ERROR, "axidraw lock: не вдалося відкрити %s", lock_path);
        return -1;
    }
    if (flock (fd, LOCK_EX | LOCK_NB) != 0) {
        trace_write (LOG_WARN, "axidraw lock: ресурс зайнятий %s", lock_path);
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
    trace_write (LOG_INFO, "axidraw lock: отримано %s fd=%d", lock_path, fd);
    return 0;
}

/**
 * @brief Зняти взаємне виключення, отримане axidraw_device_lock_acquire().
 *
 * @param fd Дескриптор lock-файла.
 */
void axidraw_device_lock_release (int fd) {
    if (fd < 0)
        return;
    flock (fd, LOCK_UN);
    close (fd);
    trace_write (LOG_INFO, "axidraw lock: звільнено fd=%d", fd);
}

/**
 * @brief Повернути шлях до lock-файла, з яким працює менеджер.
 *
 * @return Нуль-термінований рядок (статичний буфер).
 */
const char *axidraw_device_lock_file (void) { return axidraw_lock_path (); }

int axidraw_emergency_stop (axidraw_device_t *dev) {
    if (axidraw_require_connection (dev) != 0)
        return -1;
    int rc = ebb_emergency_stop (dev->port, dev->timeout_ms);
    if (rc == 0) {
        axidraw_reset_runtime (dev);
        trace_write (LOG_WARN, "axidraw: аварійна зупинка виконана");
    } else {
        trace_write (LOG_ERROR, "axidraw: аварійна зупинка повернула %d", rc);
    }
    return rc;
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
    dev->settings.min_cmd_interval_ms = dev->min_cmd_interval;
    LOGD (AXIDRAW_LOG("Мінімальний інтервал між командами %.2f мс"), dev->min_cmd_interval);
    trace_write (LOG_INFO, "axidraw rate limit: %.2f мс", dev->min_cmd_interval);
}

/**
 * @brief Встановити максимальний розмір черги команд у пристрої.
 *
 * @param dev               Структура пристрою.
 * @param max_fifo_commands 0 → не обмежувати; інше — кількість команд у польоті.
 */
void axidraw_set_fifo_limit (axidraw_device_t *dev, size_t max_fifo_commands) {
    if (!dev)
        return;
    dev->max_fifo_commands = max_fifo_commands;
    dev->settings.fifo_limit = max_fifo_commands;
    if (max_fifo_commands == 0) {
        LOGD (AXIDRAW_LOG("Ліміт FIFO команд вимкнено"));
        trace_write (LOG_INFO, "axidraw fifo: ліміт вимкнено");
    } else {
        LOGD (AXIDRAW_LOG("Ліміт FIFO команд %zu"), dev->max_fifo_commands);
        trace_write (LOG_INFO, "axidraw fifo: ліміт %zu", dev->max_fifo_commands);
    }
}

/**
 * @brief Перетворити відсоткове положення у значення SC (одиниці 83.3 нс).
 *
 * @param percent Положення у відсотках (0..100).
 * @return Значення в діапазоні [1,65535], яке можна передати у SC,4/SC,5.
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
 * @brief Перетворити швидкість сервоприводу у значення SC,11/SC,12.
 *
 * @param speed_percent Швидкість у відсотках/с (очікуваний діапазон ≥ 0).
 * @return Значення у межах 0..65535 (після масштабування).
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
 * @brief Переконатися, що пристрій підключено.
 *
 * @param dev Структура пристрою.
 * @return 0 якщо підключено; -1 інакше.
 */
static int axidraw_require_connection (axidraw_device_t *dev) {
    if (!dev || !dev->connected || !dev->port) {
        LOGE (AXIDRAW_LOG("Пристрій не підключено"));
        trace_write (LOG_ERROR, "axidraw: пристрій не підключено");
        return -1;
    }
    return 0;
}

/**
 * @brief Оновити локальні дані про кількість команд у черзі EBB.
 *
 * @param dev Структура пристрою.
 * @return 0 при успіху; -1 при помилці читання QM.
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
    trace_write (
        LOG_DEBUG,
        "axidraw fifo: активні=%zu queued=%zu (разом %zu)",
        active,
        queued,
        dev->pending_commands);
    return 0;
}

/**
 * @brief Дочекатися наявності місця у FIFO EBB.
 *
 * @param dev Структура пристрою.
 * @return 0 при успіху; -1 при тайм-ауті або помилці опитування.
 */
static int axidraw_wait_queue_slot (axidraw_device_t *dev) {
    if (!dev)
        return -1;
    if (dev->max_fifo_commands == 0)
        return 0;
    if (dev->pending_commands < dev->max_fifo_commands)
        return 0;

    trace_write (
        LOG_DEBUG,
        "axidraw fifo: очікування місця (%zu/%zu)",
        dev->pending_commands,
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
            trace_write (
                LOG_DEBUG,
                "axidraw fifo: ще зайнято (%zu/%zu)",
                dev->pending_commands,
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
        double waited = timespec_diff_ms (&now, &start);
        if ((int)waited >= dev->timeout_ms) {
            LOGE (AXIDRAW_LOG("Перевищено тайм-аут очікування FIFO"));
            trace_write (LOG_ERROR, "axidraw fifo: тайм-аут очікування (>%d мс)", dev->timeout_ms);
            return -1;
        }
    }

    trace_write (
        LOG_DEBUG,
        "axidraw fifo: місце отримано (%zu/%zu)",
        dev->pending_commands,
        dev->max_fifo_commands);

    return 0;
}

/**
 * @brief Дочекатися, доки мине мінімальний інтервал між командами.
 *
 * @param dev Структура пристрою.
 * @return 0 при успіху; -1 якщо отримати час не вдалося.
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
        double elapsed = timespec_diff_ms (&now, &dev->last_cmd);
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
            LOGD (AXIDRAW_LOG("Затримка %.2f мс перед наступною командою"), remaining);
            logged = true;
            trace_write (LOG_DEBUG, "axidraw rate: очікування %.2f мс", remaining);
        }
        nanosleep (&ts, NULL);
    }
}

/**
 * @brief Дочекатися доступного слота з урахуванням FIFO та інтервалу.
 *
 * @param dev Структура пристрою.
 * @return 0 при успіху; -1 при помилці очікування.
 */
static int axidraw_wait_slot (axidraw_device_t *dev) {
    if (axidraw_wait_queue_slot (dev) != 0)
        return -1;
    if (axidraw_wait_interval (dev) != 0)
        return -1;
    trace_write (LOG_DEBUG, "axidraw: слот доступний для наступної команди");
    return 0;
}

/** Зафіксувати відправлення команди: час та лічильник. */
static void axidraw_mark_dispatched (axidraw_device_t *dev) {
    if (!dev)
        return;
    struct timespec now;
    if (clock_gettime (CLOCK_MONOTONIC, &now) == 0)
        dev->last_cmd = now;
    if (dev->pending_commands < SIZE_MAX)
        ++dev->pending_commands;
    trace_write (
        LOG_DEBUG,
        "axidraw fifo: відправлено, у черзі %zu", dev->pending_commands);
}

/**
 * @brief Синхронізувати налаштування з прошивкою EBB.
 *
 * Надсилає команди SC/SR для позицій пера, швидкостей та тайм-ауту сервоприводу.
 * Вимагає активного підключення.
 *
 * @param dev Пристрій AxiDraw.
 */
static void axidraw_sync_settings (axidraw_device_t *dev) {
    if (!dev)
        return;
    if (axidraw_require_connection (dev) != 0)
        return;

    const axidraw_settings_t *cfg = &dev->settings;

    trace_write (LOG_DEBUG, "axidraw settings: синхронізація з пристроєм");

    /* Завжди гарантуємо, що використовується сервопривід */
    if (ebb_configure_mode (dev->port, 1, 1, dev->timeout_ms) != 0) {
        LOGW (AXIDRAW_LOG("Не вдалося активувати сервопривід (SC,1,1)"));
        trace_write (LOG_WARN, "axidraw settings: SC,1,1 не виконано");
    }

    if (cfg->pen_up_pos >= 0) {
        int up = axidraw_percent_to_servo (cfg->pen_up_pos);
        if (ebb_configure_mode (dev->port, 4, up, dev->timeout_ms) != 0) {
            LOGW (AXIDRAW_LOG("Не вдалося налаштувати позицію пера вгору (SC,4,%d)"), up);
            trace_write (LOG_WARN, "axidraw settings: SC,4,%d відхилено", up);
        } else {
            LOGD (
                AXIDRAW_LOG("Позиція пера вгору %d%% → %d"), cfg->pen_up_pos, up);
            trace_write (
                LOG_DEBUG,
                "axidraw settings: SC,4,%d (%.0f%%)",
                up,
                (double)cfg->pen_up_pos);
        }
    }

    if (cfg->pen_down_pos >= 0) {
        int down = axidraw_percent_to_servo (cfg->pen_down_pos);
        if (ebb_configure_mode (dev->port, 5, down, dev->timeout_ms) != 0) {
            LOGW (AXIDRAW_LOG("Не вдалося налаштувати позицію пера вниз (SC,5,%d)"), down);
            trace_write (LOG_WARN, "axidraw settings: SC,5,%d відхилено", down);
        } else {
            LOGD (
                AXIDRAW_LOG("Позиція пера вниз %d%% → %d"), cfg->pen_down_pos, down);
            trace_write (
                LOG_DEBUG,
                "axidraw settings: SC,5,%d (%.0f%%)",
                down,
                (double)cfg->pen_down_pos);
        }
    }

    if (cfg->pen_up_speed >= 0) {
        int up_speed = axidraw_speed_to_rate (cfg->pen_up_speed);
        if (ebb_configure_mode (dev->port, 11, up_speed, dev->timeout_ms) != 0) {
            LOGW (
                AXIDRAW_LOG("Не вдалося налаштувати швидкість підйому пера (SC,11,%d)"),
                up_speed);
            trace_write (LOG_WARN, "axidraw settings: SC,11,%d відхилено", up_speed);
        } else {
            trace_write (LOG_DEBUG, "axidraw settings: SC,11,%d", up_speed);
        }
    }

    if (cfg->pen_down_speed >= 0) {
        int down_speed = axidraw_speed_to_rate (cfg->pen_down_speed);
        if (ebb_configure_mode (dev->port, 12, down_speed, dev->timeout_ms) != 0) {
            LOGW (
                AXIDRAW_LOG("Не вдалося налаштувати швидкість опускання пера (SC,12,%d)"),
                down_speed);
            trace_write (LOG_WARN, "axidraw settings: SC,12,%d відхилено", down_speed);
        } else {
            trace_write (LOG_DEBUG, "axidraw settings: SC,12,%d", down_speed);
        }
    }

    if (cfg->servo_timeout_s >= 0) {
        uint64_t timeout_ms = (uint64_t)cfg->servo_timeout_s * 1000ULL;
        if (timeout_ms > UINT32_MAX)
            timeout_ms = UINT32_MAX;
        if (ebb_set_servo_power_timeout (dev->port, (uint32_t)timeout_ms, 1, dev->timeout_ms) != 0) {
            LOGW (
                AXIDRAW_LOG("Не вдалося налаштувати тайм-аут сервоприводу (SR,%llu)"),
                (unsigned long long)timeout_ms);
            trace_write (
                LOG_WARN,
                "axidraw settings: SR,%llu відхилено",
                (unsigned long long)timeout_ms);
        } else {
            trace_write (LOG_DEBUG, "axidraw settings: SR,%llu", (unsigned long long)timeout_ms);
        }
    }
}

/**
 * @brief Надіслати команду SP з урахуванням налаштувань затримки.
 *
 * @param dev    Структура пристрою.
 * @param pen_up true → підняти перо; false → опустити.
 * @return 0 при успіху; -1 при помилці/відсутності з’єднання.
 */
static int axidraw_exec_pen (axidraw_device_t *dev, bool pen_up) {
    if (axidraw_require_connection (dev) != 0)
        return -1;
    if (axidraw_wait_slot (dev) != 0)
        return -1;
    int delay_ms = pen_up ? dev->settings.pen_up_delay_ms : dev->settings.pen_down_delay_ms;
    if (delay_ms < 0)
        delay_ms = 0;
    LOGD (
        AXIDRAW_LOG("Перо %s (затримка %d мс)"), pen_up ? "вгору" : "вниз", delay_ms);
    trace_write (
        LOG_DEBUG,
        "axidraw pen: %s delay=%d", pen_up ? "up" : "down", delay_ms);
    int rc = ebb_pen_set (dev->port, pen_up, delay_ms, -1, dev->timeout_ms);
    if (rc == 0) {
        axidraw_mark_dispatched (dev);
        trace_write (LOG_DEBUG, "axidraw pen: команда успішна");
    } else {
        LOGE (AXIDRAW_LOG("Команда пера повернула помилку (%d)"), rc);
        trace_write (LOG_ERROR, "axidraw pen: помилка %d", rc);
    }
    return rc;
}

/** Підняти перо (SP,1). */
int axidraw_pen_up (axidraw_device_t *dev) { return axidraw_exec_pen (dev, true); }

/** Опустити перо (SP,0). */
int axidraw_pen_down (axidraw_device_t *dev) { return axidraw_exec_pen (dev, false); }

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
    if (axidraw_wait_slot (dev) != 0)
        return -1;
    LOGD (AXIDRAW_LOG("Рух duration=%u a=%d b=%d"), duration, a, b);
    trace_write (
        LOG_DEBUG,
        "axidraw SM: duration=%u a=%d b=%d", duration, a, b);
    int rc = fn (dev->port, duration, a, b, dev->timeout_ms);
    if (rc == 0) {
        axidraw_mark_dispatched (dev);
        trace_write (LOG_DEBUG, "axidraw SM: команда успішна");
    } else {
        LOGE (AXIDRAW_LOG("Команда руху повернула помилку (%d)"), rc);
        trace_write (LOG_ERROR, "axidraw SM: помилка %d", rc);
    }
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
    if (axidraw_wait_slot (dev) != 0)
        return -1;
    trace_write (
        LOG_DEBUG,
        "axidraw XM: duration=%u a=%d b=%d", duration_ms, steps_a, steps_b);
    int rc = ebb_move_mixed (dev->port, duration_ms, steps_a, steps_b, dev->timeout_ms);
    if (rc == 0) {
        axidraw_mark_dispatched (dev);
        trace_write (LOG_DEBUG, "axidraw XM: команда успішна");
    } else {
        LOGE (AXIDRAW_LOG("Команда XM повернула помилку (%d)"), rc);
        trace_write (LOG_ERROR, "axidraw XM: помилка %d", rc);
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
    if (axidraw_wait_slot (dev) != 0)
        return -1;
    LOGD (
        AXIDRAW_LOG("LM rate1=%u steps1=%d rate2=%u steps2=%d"), rate1, steps1, rate2, steps2);
    trace_write (
        LOG_DEBUG,
        "axidraw LM: rate1=%u steps1=%d accel1=%d rate2=%u steps2=%d accel2=%d flags=%d",
        rate1,
        steps1,
        accel1,
        rate2,
        steps2,
        accel2,
        clear_flags);
    int rc = ebb_move_lowlevel_steps (
        dev->port, rate1, steps1, accel1, rate2, steps2, accel2, clear_flags, dev->timeout_ms);
    if (rc == 0) {
        axidraw_mark_dispatched (dev);
        trace_write (LOG_DEBUG, "axidraw LM: команда успішна");
    } else {
        LOGE (AXIDRAW_LOG("Команда LM повернула помилку (%d)"), rc);
        trace_write (LOG_ERROR, "axidraw LM: помилка %d", rc);
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
    if (axidraw_wait_slot (dev) != 0)
        return -1;
    LOGD (AXIDRAW_LOG("LT intervals=%u rate1=%d rate2=%d"), intervals, rate1, rate2);
    trace_write (
        LOG_DEBUG,
        "axidraw LT: intervals=%u rate1=%d accel1=%d rate2=%d accel2=%d flags=%d",
        intervals,
        rate1,
        accel1,
        rate2,
        accel2,
        clear_flags);
    int rc = ebb_move_lowlevel_time (
        dev->port, intervals, rate1, accel1, rate2, accel2, clear_flags, dev->timeout_ms);
    if (rc == 0) {
        axidraw_mark_dispatched (dev);
        trace_write (LOG_DEBUG, "axidraw LT: команда успішна");
    } else {
        LOGE (AXIDRAW_LOG("Команда LT повернула помилку (%d)"), rc);
        trace_write (LOG_ERROR, "axidraw LT: помилка %d", rc);
    }
    return rc;
}

/** Виклик HM для повернення в home/абсолютну позицію. */
int axidraw_home (
    axidraw_device_t *dev, uint32_t step_rate, const int32_t *pos1, const int32_t *pos2) {
    if (axidraw_require_connection (dev) != 0)
        return -1;
    if (axidraw_wait_slot (dev) != 0)
        return -1;
    LOGD (
        "axidraw: HM step_rate=%u pos1=%d pos2=%d", step_rate, pos1 ? *pos1 : 0, pos2 ? *pos2 : 0);
    trace_write (
        LOG_DEBUG,
        "axidraw HM: step_rate=%u pos1=%d pos2=%d",
        step_rate,
        pos1 ? *pos1 : 0,
        pos2 ? *pos2 : 0);
    int rc = ebb_home_move (dev->port, step_rate, pos1, pos2, dev->timeout_ms);
    if (rc == 0) {
        axidraw_mark_dispatched (dev);
        trace_write (LOG_DEBUG, "axidraw HM: команда успішна");
    } else {
        LOGE (AXIDRAW_LOG("Команда HM повернула помилку (%d)"), rc);
        trace_write (LOG_ERROR, "axidraw HM: помилка %d", rc);
    }
    return rc;
}

/** Зібрати агрегований статус пристрою. */
int axidraw_status (axidraw_device_t *dev, ebb_status_snapshot_t *snapshot) {
    if (axidraw_require_connection (dev) != 0)
        return -1;
    int rc = ebb_collect_status (dev->port, snapshot, dev->timeout_ms);
    if (rc != 0) {
        LOGE (AXIDRAW_LOG("Не вдалося зібрати статус (код %d)"), rc);
        trace_write (LOG_ERROR, "axidraw статус: помилка %d", rc);
    } else {
        LOGD (
            "axidraw: статус motion=%d/%d/%d fifo=%d pen=%d", snapshot->motion.command_active,
            snapshot->motion.motor1_active, snapshot->motion.motor2_active,
            snapshot->motion.fifo_pending, snapshot->pen_up);
        trace_write (
            LOG_DEBUG,
            "axidraw статус: active=%d m1=%d m2=%d fifo=%d pen=%d",
            snapshot->motion.command_active,
            snapshot->motion.motor1_active,
            snapshot->motion.motor2_active,
            snapshot->motion.fifo_pending,
            snapshot->pen_up);
    }
    return rc;
}
