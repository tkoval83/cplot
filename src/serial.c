/**
 * @file serial.c
 * @brief Реалізація відкриття/читання/запису серійного порту.
 * @ingroup serial
 * @details
 * Використовує POSIX `termios` для налаштування raw‑режиму, `poll(2)` для
 * тайм‑аутів та неблокуючі операції `read(2)`/`write(2)`. Кодування і протокол
 * вищого рівня (EBB) залишаються за викликачем.
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
 * @brief Внутрішній стан відкритого порту.
 */
struct serial_port_s {
    int fd;                 /**< Файловий дескриптор порту. */
    int default_timeout_ms; /**< Тайм‑аут читання за замовчуванням, мс. */
};

/**
 * @brief Перетворює числову швидкість у константу termios.
 * @param baud Швидкість у бодах (9600, 115200, ...).
 * @return Значення `speed_t` для termios (B9600 тощо).
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
        return B115200;
    }
}

/**
 * @copydoc serial_open
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
        tio.c_cflag &= ~CRTSCTS;
        tio.c_iflag &= ~(IXON | IXOFF | IXANY);

        speed_t spd = baud_to_speed (baud);
        cfsetispeed (&tio, spd);
        cfsetospeed (&tio, spd);

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
 * @copydoc serial_close
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
 * @copydoc serial_write
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
                return (ssize_t)(len - left);
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
 * @copydoc serial_read
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
                break;
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
            break;
        got += (size_t)rd;

        break;
    }
    return (ssize_t)got;
}

/**
 * @copydoc serial_flush_input
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
 * @copydoc serial_write_line
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
 * @copydoc serial_read_line
 */
ssize_t serial_read_line (serial_port_t *sp, char *buf, size_t maxlen, int timeout_ms) {
    if (!sp || !buf || maxlen == 0)
        return -1;
    size_t pos = 0;
    int elapsed = 0;
    const int step = 20;
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
            buf[pos++] = ch;
    }
    return 0;
}

/**
 * @copydoc serial_probe_ebb
 */
int serial_probe_ebb (serial_port_t *sp, char *version_out, size_t version_len) {
    if (!sp)
        return -1;

    serial_flush_input (sp);

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
 * @copydoc serial_guess_axidraw_port
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
