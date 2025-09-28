/**
 * @file serial.c
 * @brief Реалізація POSIX‑послідовного порту для EBB/AxiDraw.
 */
#include "serial.h"

#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/**
 * Внутрішнє представлення дескриптора послідовного порту.
 *
 * @note Структура непрозора для користувачів модуля; оголошена як
 *       іменований тип у заголовку (opaque pointer).
 */
struct serial_port_s {
    int fd;
    int default_timeout_ms;
};

/**
 * Перетворити ціле значення швидкості у відповідну константу termios.
 *
 * @param baud Цільова швидкість у бодах (напр., 115200).
 * @return Константа типу speed_t; за замовчуванням B115200 для невідомих значень.
 */
static speed_t baud_to_speed (int baud) {
    switch (baud) {
    case 9600:
        return B9600;
    case 19200:
        return B19200;
    case 38400:
        return B38400;
    case 57600:
        return B57600;
    case 115200:
        return B115200;
#ifdef B230400
    case 230400:
        return B230400;
#endif
#ifdef B460800
    case 460800:
        return B460800;
#endif
    default:
        return B115200; /* безпечне типове */
    }
}

/**
 * Див. опис у заголовку: відкрити і налаштувати послідовний порт (8N1, RAW).
 *
 * @param path Шлях до пристрою (наприклад, "/dev/tty.usbmodem*").
 * @param baud Швидкість у бодах.
 * @param read_timeout_ms Типовий тайм‑аут читання (мс) для операцій рядка.
 * @param errbuf Буфер для повідомлення про помилку (може бути NULL).
 * @param errlen Розмір буфера помилки (0, якщо errbuf == NULL).
 * @return Вказівник на відкритий порт або NULL у разі помилки.
 */
serial_port_t *
serial_open (const char *path, int baud, int read_timeout_ms, char *errbuf, size_t errlen) {
    if (!path || !*path) {
        if (errbuf && errlen)
            snprintf (errbuf, errlen, "не вказано шлях до порту");
        log_print (LOG_ERROR, "послідовний: не вказано шлях до порту");
        return NULL;
    }
    int fd = open (path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        if (errbuf && errlen)
            snprintf (errbuf, errlen, "open('%s'): %s", path, strerror (errno));
        log_print (LOG_ERROR, "послідовний: помилка open('%s'): %s", path, strerror (errno));
        return NULL;
    }

    /* Заборона перетворень терміналу */
    if (isatty (fd)) {
        struct termios tio;
        if (tcgetattr (fd, &tio) != 0) {
            if (errbuf && errlen)
                snprintf (errbuf, errlen, "tcgetattr: %s", strerror (errno));
            close (fd);
            log_print (
                LOG_ERROR, "послідовний: tcgetattr для '%s' завершився помилкою %s", path,
                strerror (errno));
            return NULL;
        }
        cfmakeraw (&tio);
        tio.c_cflag |= (CLOCAL | CREAD);
        tio.c_cflag &= ~CRTSCTS;                /* без апаратного flow control */
        tio.c_iflag &= ~(IXON | IXOFF | IXANY); /* без програмного flow control */

        speed_t spd = baud_to_speed (baud);
        cfsetispeed (&tio, spd);
        cfsetospeed (&tio, spd);

        /* Мінімальний тайм‑аут для read(2) на рівні драйвера не ставимо; використовуємо poll */
        tio.c_cc[VMIN] = 0;
        tio.c_cc[VTIME] = 0;

        if (tcsetattr (fd, TCSANOW, &tio) != 0) {
            if (errbuf && errlen)
                snprintf (errbuf, errlen, "tcsetattr: %s", strerror (errno));
            close (fd);
            log_print (
                LOG_ERROR, "послідовний: tcsetattr для '%s' завершився помилкою %s", path,
                strerror (errno));
            return NULL;
        }
    }

    int flags = fcntl (fd, F_GETFL, 0);
    fcntl (fd, F_SETFL, flags | O_NONBLOCK);

    serial_port_t *sp = (serial_port_t *)calloc (1, sizeof (*sp));
    if (!sp) {
        if (errbuf && errlen)
            snprintf (errbuf, errlen, "нестача пам’яті");
        close (fd);
        log_print (LOG_ERROR, "serial: нестача пам'яті для дескриптора '%s'", path);
        return NULL;
    }
    sp->fd = fd;
    sp->default_timeout_ms = (read_timeout_ms > 0) ? read_timeout_ms : 1000;

    LOGD ("відкрито порт: %s @ %d бод", path, baud);
    log_print (
        LOG_INFO, "послідовний: відкрито %s @%d бод тайм-аут=%d", path, baud,
        sp->default_timeout_ms);
    return sp;
}

/**
 * Закрити порт і звільнити пам’ять під дескриптор.
 * @param sp Порт або NULL.
 */
void serial_close (serial_port_t *sp) {
    if (!sp)
        return;
    if (sp->fd >= 0) {
        log_print (LOG_INFO, "послідовний: закрито fd=%d", sp->fd);
        close (sp->fd);
    }
    free (sp);
}

/**
 * Записати у порт з очікуванням готовності до запису через poll(2).
 *
 * @param sp Порт.
 * @param data Дані.
 * @param len Довжина даних.
 * @param timeout_ms Загальний тайм‑аут на запис (мс); частковий запис повертає кількість.
 * @return Кількість записаних байтів; -1 при помилці.
 */
ssize_t serial_write (serial_port_t *sp, const void *data, size_t len, int timeout_ms) {
    if (!sp || sp->fd < 0 || !data)
        return -1;
    const uint8_t *p = (const uint8_t *)data;
    size_t left = len;
    int tmo = (timeout_ms > 0) ? timeout_ms : 2000;

    while (left > 0) {
        struct pollfd pfd = { .fd = sp->fd, .events = POLLOUT };
        int pr = poll (&pfd, 1, tmo);
        if (pr <= 0) {
            if (pr == 0)
                return (ssize_t)(len - left); /* тайм‑аут, частковий запис */
            if (errno == EINTR)
                continue;
            return -1;
        }
        ssize_t wr = write (sp->fd, p, left);
        if (wr < 0) {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            return -1;
        }
        left -= (size_t)wr;
        p += wr;
    }
    return (ssize_t)len;
}

/**
 * Прочитати з порту з тайм‑аутом готовності через poll(2).
 *
 * @param sp Порт.
 * @param buf Буфер призначення.
 * @param len Максимум байтів до читання.
 * @param timeout_ms Тайм‑аут (мс) на очікування даних.
 * @return Кількість прочитаних байтів (>0), 0 при тайм‑ауті, -1 при помилці.
 */
ssize_t serial_read (serial_port_t *sp, void *buf, size_t len, int timeout_ms) {
    if (!sp || sp->fd < 0 || !buf || len == 0)
        return -1;
    size_t got = 0;
    int tmo = (timeout_ms > 0) ? timeout_ms : sp->default_timeout_ms;

    while (got < len) {
        struct pollfd pfd = { .fd = sp->fd, .events = POLLIN };
        int pr = poll (&pfd, 1, tmo);
        if (pr <= 0) {
            if (pr == 0)
                break; /* тайм‑аут */
            if (errno == EINTR)
                continue;
            return -1;
        }
        ssize_t rd = read (sp->fd, (uint8_t *)buf + got, len - got);
        if (rd < 0) {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            return -1;
        }
        if (rd == 0)
            break; /* EOF? */
        got += (size_t)rd;
        /* Одного poll достатньо для більшості застосувань; завершуємо одразу після прийому. */
        break;
    }
    return (ssize_t)got;
}

/**
 * Скинути/прочистити вхідний буфер порту.
 * @param sp Порт.
 * @return Кількість відкинутих байтів (>=0) або -1 при помилці.
 */
int serial_flush_input (serial_port_t *sp) {
    if (!sp || sp->fd < 0)
        return -1;
    uint8_t tmp[256];
    int total = 0;
    while (1) {
        ssize_t rd = read (sp->fd, tmp, sizeof tmp);
        if (rd <= 0) {
            if (rd < 0 && (errno == EAGAIN || errno == EINTR)) {
                struct pollfd pfd = { .fd = sp->fd, .events = POLLIN };
                if (poll (&pfd, 1, 10) <= 0)
                    break;
                continue;
            }
            break;
        }
        total += (int)rd;
        if (rd < (ssize_t)sizeof tmp)
            break;
    }
    return total;
}

/**
 * Відправити рядок ASCII та завершити CR (\r), як очікує EBB.
 * @param sp Порт.
 * @param s Рядок без CR/LF.
 * @return 0 якщо весь рядок і CR надіслані; -1 інакше.
 */
int serial_write_line (serial_port_t *sp, const char *s) {
    if (!sp || !s)
        return -1;
    size_t n = strlen (s);
    if (serial_write (sp, s, n, 2000) < 0)
        return -1;
    const char cr = '\r';
    if (serial_write (sp, &cr, 1, 200) < 0)
        return -1;
    return 0;
}

/**
 * Прочитати один рядок, завершений CR або LF. Термінаційний символ не включається.
 * @param sp Порт.
 * @param buf Буфер призначення (буде нуль‑термінований).
 * @param maxlen Максимальна довжина буфера (включно з нуль‑термінатором).
 * @param timeout_ms Загальний тайм‑аут (мс).
 * @return Довжина рядка (без термінатора); 0 при тайм‑ауті; -1 при помилці.
 */
ssize_t serial_read_line (serial_port_t *sp, char *buf, size_t maxlen, int timeout_ms) {
    if (!sp || !buf || maxlen == 0)
        return -1;
    size_t pos = 0;
    int elapsed = 0;
    const int step = 20; /* мс */
    while (elapsed <= timeout_ms) {
        char ch;
        ssize_t rd = serial_read (sp, &ch, 1, step);
        if (rd < 0)
            return -1;
        if (rd == 0) {
            elapsed += step;
            continue;
        }
        if (ch == '\r' || ch == '\n') {
            if (pos < maxlen)
                buf[pos] = '\0';
            return (ssize_t)pos;
        }
        if (pos + 1 < maxlen)
            buf[pos++] = ch; /* +1 для нуля */
    }
    return 0; /* тайм‑аут */
}

/**
 * Надіслати команду "V" та прочитати відповідь версії від EBB.
 * @param sp Відкритий порт.
 * @param version_out Куди скопіювати відповідь (може бути NULL).
 * @param version_len Довжина буфера відповіді.
 * @return 0 при успіху; -1 при помилці/тайм‑ауті.
 */
int serial_probe_ebb (serial_port_t *sp, char *version_out, size_t version_len) {
    if (!sp)
        return -1;
    /* Очистити вхід */
    serial_flush_input (sp);
    /* V — Version Query (див. docs/ebb.md) */
    if (serial_write_line (sp, "V") != 0)
        return -1;
    char line[128];
    ssize_t n = serial_read_line (sp, line, sizeof line, sp->default_timeout_ms);
    if (n <= 0)
        return -1;
    if (version_out && version_len) {
        strncpy (version_out, line, version_len - 1);
        version_out[version_len - 1] = '\0';
    }
    LOGD ("EBB відповів: %s", line);
    return 0;
}

#ifdef __APPLE__
#include <dirent.h>
/**
 * Спробувати знайти пристрій AxiDraw на macOS, шукаючи /dev/tty.usbmodem*.
 * @param out_path Буфер для запису повного шляху до пристрою.
 * @param out_len Розмір буфера.
 * @return 0 якщо знайдено; -1 якщо не знайдено або помилка.
 */
int serial_guess_axidraw_port (char *out_path, size_t out_len) {
    if (!out_path || out_len == 0)
        return -1;
    DIR *d = opendir ("/dev");
    if (!d)
        return -1;
    struct dirent *e;
    int rc = -1;
    while ((e = readdir (d)) != NULL) {
        if (strncmp (e->d_name, "tty.usbmodem", 12) == 0) {
            int written = snprintf (out_path, out_len, "/dev/%s", e->d_name);
            if (written < 0 || (size_t)written >= out_len) {
                LOGW ("шлях до tty занадто довгий: %s", e->d_name);
                rc = -1;
            } else {
                rc = 0;
                break;
            }
        }
    }
    closedir (d);
    return rc;
}
#endif
