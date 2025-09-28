/**
 * @file ebb.c
 * @brief Реалізація високорівневих команд EBB (рух, мотори, перо).
 */

#include "ebb.h"

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

/** Максимальна довжина рядка команди EBB. */
#define EBB_CMD_MAX 128
/** Максимальна довжина рядка відповіді. */
#define EBB_RESP_MAX 128

/**
 * Надіслати команду у форматі printf і прочитати підтвердження OK.
 *
 * @param sp         Відкритий послідовний порт.
 * @param timeout_ms Тайм-аут очікування відповіді.
 * @param fmt        Формат команди без завершального CR.
 * @param ap         Аргументи формату.
 * @return 0 при успіху; -1 при помилці запису/читання або відповіді ≠ OK.
 */
static int ebb_send_vcommand (serial_port_t *sp, int timeout_ms, const char *fmt, va_list ap) {
    if (!sp || !fmt)
        return -1;

    char cmd[EBB_CMD_MAX];
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
    int written = vsnprintf (cmd, sizeof (cmd), fmt, ap);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    if (written < 0 || (size_t)written >= sizeof (cmd)) {
        LOGE ("Команда контролера надто довга або форматована з помилкою");
        return -1;
    }

    LOGD ("контролер → %s", cmd);
    log_print (LOG_DEBUG, "контролер → %s", cmd);
    if (serial_write_line (sp, cmd) != 0) {
        LOGE ("Не вдалося надіслати команду до контролера");
        return -1;
    }

    char resp[EBB_RESP_MAX];
    for (int attempt = 0; attempt < 8; ++attempt) {
        ssize_t len = serial_read_line (sp, resp, sizeof (resp), timeout_ms);
        if (len <= 0) {
            LOGE ("Контролер не відповів на команду або стався тайм-аут");
            log_print (LOG_ERROR, "контролер: відсутня відповідь або тайм-аут на '%s'", cmd);
            return -1;
        }
        resp[len] = '\0';
        LOGD ("контролер ← %s", resp);
        log_print (LOG_DEBUG, "контролер ← %s", resp);
        if (strcmp (resp, "OK") == 0)
            return 0;
        if (strncmp (resp, "ERR", 3) == 0 || resp[0] == '!') {
            LOGE ("Контролер повернув помилку: %s", resp);
            log_print (LOG_ERROR, "контролер: помилка відповіді '%s'", resp);
            return -1;
        }
        /* Інформаційні рядки ігноруємо й читаємо наступний. */
    }

    LOGE ("Контролер не надіслав підтвердження ОК після команди");
    log_print (LOG_ERROR, "контролер: не отримав ОК після '%s'", cmd);
    return -1;
}

/**
 * @brief Надіслати printf-команду та дочекатися підтвердження `OK`.
 *
 * @param sp         Послідовний порт.
 * @param timeout_ms Тайм-аут відповіді.
 * @param fmt        Формат командного рядка без `CR`.
 * @return 0 при успіху, -1 при помилці.
 */
static int ebb_send_command (serial_port_t *sp, int timeout_ms, const char *fmt, ...) {
    va_list ap;
    va_start (ap, fmt);
    int rc = ebb_send_vcommand (sp, timeout_ms, fmt, ap);
    va_end (ap);
    return rc;
}

/**
 * @brief Надіслати printf-запит і повернути перший рядок відповіді.
 *
 * Відповідно до документації EBB (docs/ebb.md) більшість команд повертають один рядок
 * даних перед `OK`. Ігнорує додаткові інформаційні рядки.
 */
static int ebb_send_vquery (
    serial_port_t *sp,
    int timeout_ms,
    char *resp_out,
    size_t resp_len,
    const char *fmt,
    va_list ap) {
    if (!sp || !fmt)
        return -1;

    char cmd[EBB_CMD_MAX];
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
    int written = vsnprintf (cmd, sizeof (cmd), fmt, ap);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    if (written < 0 || (size_t)written >= sizeof (cmd)) {
        LOGE ("Команда контролера надто довга або форматована з помилкою");
        return -1;
    }

    LOGD ("контролер → %s", cmd);
    log_print (LOG_DEBUG, "контролер → %s", cmd);
    if (serial_write_line (sp, cmd) != 0) {
        LOGE ("Не вдалося надіслати команду до контролера");
        return -1;
    }

    bool need_data = resp_out && resp_len > 0;
    bool data_received = false;
    char resp[EBB_RESP_MAX];
    for (int attempt = 0; attempt < 8; ++attempt) {
        ssize_t len = serial_read_line (sp, resp, sizeof (resp), timeout_ms);
        if (len <= 0) {
            LOGE ("Контролер не відповів на запит або стався тайм-аут");
            log_print (LOG_ERROR, "контролер: тайм-аут або тиша на запит '%s'", cmd);
            return -1;
        }
        resp[len] = '\0';
        LOGD ("контролер ← %s", resp);
        log_print (LOG_DEBUG, "контролер ← %s", resp);
        if (strcmp (resp, "OK") == 0) {
            if (need_data && !data_received) {
                LOGE ("Контролер не повернув дані у відповіді");
                log_print (LOG_ERROR, "контролер: очікував дані від '%s', отримано лише ОК", cmd);
                return -1;
            }
            return 0;
        }
        if (strncmp (resp, "ERR", 3) == 0 || resp[0] == '!') {
            LOGE ("Контролер повернув помилку: %s", resp);
            log_print (LOG_ERROR, "контролер: помилка відповіді '%s'", resp);
            return -1;
        }
        if (need_data && !data_received) {
            strncpy (resp_out, resp, resp_len - 1);
            resp_out[resp_len - 1] = '\0';
            data_received = true;
            log_print (LOG_DEBUG, "дані контролера: %s", resp_out);
        }
        /* Отримані додаткові рядки ігноруємо. */
    }

    LOGE ("Контролер не надіслав підтвердження ОК після запиту");
    log_print (LOG_ERROR, "контролер: не отримав ОК після запиту '%s'", cmd);
    return -1;
}

/**
 * @brief Надіслати запит і отримати перший рядок відповіді до `OK`.
 *
 * @param sp         Послідовний порт.
 * @param timeout_ms Тайм-аут.
 * @param resp_out   Буфер для відповіді (може бути NULL).
 * @param resp_len   Розмір буфера.
 * @param fmt        Формат команди.
 * @return 0 успіх; -1 помилка.
 */
static int ebb_send_query (
    serial_port_t *sp, int timeout_ms, char *resp_out, size_t resp_len, const char *fmt, ...) {
    va_list ap;
    va_start (ap, fmt);
    int rc = ebb_send_vquery (sp, timeout_ms, resp_out, resp_len, fmt, ap);
    va_end (ap);
    return rc;
}

/**
 * @brief Виклик команди `EM` (Enable Motors).
 *
 * Див. розділ "Motor Control" у docs/ebb.md. Дозволяє встановити мікрошаг для
 * кожного мотору або вимкнути драйвер.
 *
 * @param sp      Послідовний порт EBB.
 * @param motor1  Режим для каналу 1.
 * @param motor2  Режим для каналу 2.
 * @param timeout_ms Тайм-аут очікування `OK`.
 * @return 0 при успіху, -1 при помилці/некоректних аргументах.
 */
int ebb_enable_motors (
    serial_port_t *sp, ebb_motor_mode_t motor1, ebb_motor_mode_t motor2, int timeout_ms) {
    if (!sp)
        return -1;
    if (motor1 < EBB_MOTOR_DISABLED || motor1 > EBB_MOTOR_STEP_FULL || motor2 < EBB_MOTOR_DISABLED
        || motor2 > EBB_MOTOR_STEP_FULL) {
        LOGE ("Неприпустимі параметри EM: %d, %d", (int)motor1, (int)motor2);
        return -1;
    }
    return ebb_send_command (sp, timeout_ms, "EM,%d,%d", (int)motor1, (int)motor2);
}

/**
 * @brief Вимкнути обидва мотори (EM,0,0).
 */
int ebb_disable_motors (serial_port_t *sp, int timeout_ms) {
    return ebb_enable_motors (sp, EBB_MOTOR_DISABLED, EBB_MOTOR_DISABLED, timeout_ms);
}

/**
 * @brief Виконати команду `SM` (Straight Move) у координатах двигунів.
 *
 * @param sp          Послідовний порт.
 * @param duration_ms Тривалість руху у мс.
 * @param steps1      Кроки для мотору 1.
 * @param steps2      Кроки для мотору 2.
 * @param timeout_ms  Тайм-аут відповіді.
 * @return 0 при успіху; -1 при некоректних параметрах або збоях I/O.
 */
int ebb_move_steps (
    serial_port_t *sp, uint32_t duration_ms, int32_t steps1, int32_t steps2, int timeout_ms) {
    if (!sp)
        return -1;
    if (duration_ms == 0 || duration_ms > 16777215u) {
        LOGE ("Тривалість SM поза діапазоном: %u", duration_ms);
        return -1;
    }
    if (steps1 < -16777215 || steps1 > 16777215 || steps2 < -16777215 || steps2 > 16777215) {
        LOGE ("Кількість кроків SM поза діапазоном: %d, %d", steps1, steps2);
        return -1;
    }
    return ebb_send_command (sp, timeout_ms, "SM,%u,%d,%d", duration_ms, steps1, steps2);
}

/**
 * @brief Виклик команди `SP` (Servo Pen).
 *
 * @param sp         Послідовний порт.
 * @param pen_up     `true` → підняти перо; `false` → опустити.
 * @param settle_ms  Затримка після руху сервоприводу (0..65535).
 * @param portb_pin  Додатковий порт B (0..7) або -1, якщо не використовується.
 * @param timeout_ms Тайм-аут відповіді.
 * @return 0 при успіху; -1 при помилці.
 */
int ebb_pen_set (serial_port_t *sp, bool pen_up, int settle_ms, int portb_pin, int timeout_ms) {
    if (!sp)
        return -1;
    if (settle_ms < 0 || settle_ms > 65535) {
        LOGE ("Тривалість затримки SP поза діапазоном: %d", settle_ms);
        return -1;
    }
    if (portb_pin < -1 || portb_pin > 7) {
        LOGE ("Параметр portB для SP некоректний: %d", portb_pin);
        return -1;
    }

    if (portb_pin >= 0)
        return ebb_send_command (
            sp, timeout_ms, "SP,%d,%d,%d", pen_up ? 1 : 0, settle_ms, portb_pin);
    if (settle_ms > 0)
        return ebb_send_command (sp, timeout_ms, "SP,%d,%d", pen_up ? 1 : 0, settle_ms);
    return ebb_send_command (sp, timeout_ms, "SP,%d", pen_up ? 1 : 0);
}

/**
 * @brief Виклик команди `XM` для змішаних координат A/B (CoreXY).
 *
 * @param sp          Послідовний порт EBB.
 * @param duration_ms Тривалість руху у мс.
 * @param steps_a     Кроки для каналу A.
 * @param steps_b     Кроки для каналу B.
 * @param timeout_ms  Тайм-аут очікування `OK`.
 */
int ebb_move_mixed (
    serial_port_t *sp, uint32_t duration_ms, int32_t steps_a, int32_t steps_b, int timeout_ms) {
    if (!sp)
        return -1;
    if (duration_ms == 0 || duration_ms > 16777215u) {
        LOGE ("Тривалість XM поза діапазоном: %" PRIu32, duration_ms);
        return -1;
    }
    if (steps_a < -16777215 || steps_a > 16777215 || steps_b < -16777215 || steps_b > 16777215) {
        LOGE ("Кроки XM поза діапазоном: %" PRId32 ", %" PRId32, steps_a, steps_b);
        return -1;
    }
    return ebb_send_command (
        sp, timeout_ms, "XM,%" PRIu32 ",%" PRId32 ",%" PRId32, duration_ms, steps_a, steps_b);
}

/** @brief Перевірити допустимість прапорів очищення LM/LT. */
static int ebb_validate_clear_flags (int clear_flags) {
    return (clear_flags == -1) || (clear_flags >= EBB_CLEAR_NONE && clear_flags <= EBB_CLEAR_BOTH);
}

/**
 * @brief Виклик `LM` (Low-level Move, step-limited).
 *
 * @param sp         Послідовний порт.
 * @param rate1      Швидкість мотору 1 (кроки/сек * 256).
 * @param steps1     Цільові кроки мотору 1.
 * @param accel1     Прискорення мотору 1.
 * @param rate2      Швидкість мотору 2.
 * @param steps2     Цільові кроки мотору 2.
 * @param accel2     Прискорення мотору 2.
 * @param clear_flags Прапори очищення FIFO (`EBB_CLEAR_*` або -1).
 * @param timeout_ms Тайм-аут відповіді.
 */
int ebb_move_lowlevel_steps (
    serial_port_t *sp,
    uint32_t rate1,
    int32_t steps1,
    int32_t accel1,
    uint32_t rate2,
    int32_t steps2,
    int32_t accel2,
    int clear_flags,
    int timeout_ms) {
    if (!sp)
        return -1;
    if (rate1 > 2147483647u || rate2 > 2147483647u) {
        LOGE ("Швидкість LM поза діапазоном: %" PRIu32 ", %" PRIu32, rate1, rate2);
        return -1;
    }
    if (!ebb_validate_clear_flags (clear_flags)) {
        LOGE ("Прапорці очищення LM некоректні: %d", clear_flags);
        return -1;
    }
    if (steps1 == 0 && steps2 == 0 && accel1 == 0 && accel2 == 0 && rate1 == 0 && rate2 == 0) {
        LOGE ("LM без руху не має сенсу");
        return -1;
    }

    if (clear_flags >= 0)
        return ebb_send_command (
            sp, timeout_ms,
            "LM,%" PRIu32 ",%" PRId32 ",%" PRId32 ",%" PRIu32 ",%" PRId32 ",%" PRId32 ",%d", rate1,
            steps1, accel1, rate2, steps2, accel2, clear_flags);

    return ebb_send_command (
        sp, timeout_ms, "LM,%" PRIu32 ",%" PRId32 ",%" PRId32 ",%" PRIu32 ",%" PRId32 ",%" PRId32,
        rate1, steps1, accel1, rate2, steps2, accel2);
}

/**
 * @brief Виклик `LT` (Low-level Move, time-limited).
 *
 * @param sp         Послідовний порт.
 * @param intervals  Кількість інтервалів (1/37500 с).
 * @param rate1      Стартовий rate мотору 1.
 * @param accel1     Прискорення мотору 1.
 * @param rate2      Стартовий rate мотору 2.
 * @param accel2     Прискорення мотору 2.
 * @param clear_flags Прапори очищення FIFO або -1.
 * @param timeout_ms Тайм-аут відповіді.
 */
int ebb_move_lowlevel_time (
    serial_port_t *sp,
    uint32_t intervals,
    int32_t rate1,
    int32_t accel1,
    int32_t rate2,
    int32_t accel2,
    int clear_flags,
    int timeout_ms) {
    if (!sp)
        return -1;
    if (intervals == 0) {
        LOGE ("LT потребує додатного часу");
        return -1;
    }
    if (!ebb_validate_clear_flags (clear_flags)) {
        LOGE ("Прапорці очищення LT некоректні: %d", clear_flags);
        return -1;
    }
    if (rate1 == 0 && rate2 == 0 && accel1 == 0 && accel2 == 0) {
        LOGE ("LT без руху не має сенсу");
        return -1;
    }

    if (clear_flags >= 0)
        return ebb_send_command (
            sp, timeout_ms, "LT,%" PRIu32 ",%" PRId32 ",%" PRId32 ",%" PRId32 ",%" PRId32 ",%d",
            intervals, rate1, accel1, rate2, accel2, clear_flags);

    return ebb_send_command (
        sp, timeout_ms, "LT,%" PRIu32 ",%" PRId32 ",%" PRId32 ",%" PRId32 ",%" PRId32, intervals,
        rate1, accel1, rate2, accel2);
}

#define EBB_HOME_MAX_POSITION 4294967

/**
 * @brief Виклик `HM` (Home Move) для повернення у задану позицію/нуль.
 *
 * @param sp        Послідовний порт.
 * @param step_rate Базова швидкість мікрошагів (2..25000).
 * @param pos1      Абсолютна позиція мотору 1 або NULL.
 * @param pos2      Абсолютна позиція мотору 2 або NULL.
 * @param timeout_ms Тайм-аут відповіді.
 */
int ebb_home_move (
    serial_port_t *sp,
    uint32_t step_rate,
    const int32_t *pos1,
    const int32_t *pos2,
    int timeout_ms) {
    if (!sp)
        return -1;
    if (step_rate < 2 || step_rate > 25000) {
        LOGE ("Швидкість HM поза діапазоном: %" PRIu32, step_rate);
        return -1;
    }
    if ((pos1 && !pos2) || (!pos1 && pos2)) {
        LOGE ("HM очікує або обидві позиції, або жодної");
        return -1;
    }
    if (pos1
        && (llabs ((long long)*pos1) > EBB_HOME_MAX_POSITION
            || llabs ((long long)*pos2) > EBB_HOME_MAX_POSITION)) {
        LOGE ("Абсолютна позиція HM поза діапазоном: %d, %d", *pos1, *pos2);
        return -1;
    }

    if (pos1)
        return ebb_send_command (
            sp, timeout_ms, "HM,%" PRIu32 ",%" PRId32 ",%" PRId32, step_rate, *pos1, *pos2);

    return ebb_send_command (sp, timeout_ms, "HM,%" PRIu32, step_rate);
}

/**
 * @brief Виклик `QS` (Query Steps).
 *
 * @param sp          Послідовний порт.
 * @param[out] steps1_out Кроки мотору 1.
 * @param[out] steps2_out Кроки мотору 2.
 * @param timeout_ms  Тайм-аут відповіді.
 */
int ebb_query_steps (serial_port_t *sp, int32_t *steps1_out, int32_t *steps2_out, int timeout_ms) {
    if (!sp || !steps1_out || !steps2_out)
        return -1;

    char line[EBB_RESP_MAX];
    if (ebb_send_query (sp, timeout_ms, line, sizeof (line), "QS") != 0)
        return -1;

    char *save = NULL;
    char *token = strtok_r (line, ",", &save);
    if (!token)
        goto parse_error;
    char *end = NULL;
    errno = 0;
    long val1 = strtol (token, &end, 10);
    if (errno != 0 || !end || *end != '\0' || val1 < INT32_MIN || val1 > INT32_MAX)
        goto parse_error;

    token = strtok_r (NULL, ",", &save);
    if (!token)
        goto parse_error;
    errno = 0;
    long val2 = strtol (token, &end, 10);
    if (errno != 0 || !end || *end != '\0' || val2 < INT32_MIN || val2 > INT32_MAX)
        goto parse_error;

    *steps1_out = (int32_t)val1;
    *steps2_out = (int32_t)val2;
    return 0;

parse_error:
    LOGE ("Некоректна відповідь QS");
    return -1;
}

/**
 * @brief Скинути лічильники кроків (`CS`).
 */
int ebb_clear_steps (serial_port_t *sp, int timeout_ms) {
    if (!sp)
        return -1;
    return ebb_send_command (sp, timeout_ms, "CS");
}

/**
 * @brief Виклик `QM` для отримання стану FIFO та моторів.
 *
 * @param sp          Послідовний порт.
 * @param[out] status_out Структура для заповнення прапорів активності.
 * @param timeout_ms Тайм-аут відповіді.
 */
int ebb_query_motion (serial_port_t *sp, ebb_motion_status_t *status_out, int timeout_ms) {
    if (!sp || !status_out)
        return -1;

    char line[EBB_RESP_MAX];
    if (ebb_send_query (sp, timeout_ms, line, sizeof (line), "QM") != 0)
        return -1;

    char *payload = line;
    if (strncmp (payload, "QM", 2) == 0) {
        payload += 2;
        if (*payload == ',')
            ++payload;
    }

    int values[4] = { 0, 0, 0, 0 };
    size_t count = 0;
    char *save = NULL;
    char *token = strtok_r (payload, ",", &save);
    while (token && count < 4) {
        char *end = NULL;
        errno = 0;
        long v = strtol (token, &end, 10);
        if (errno != 0 || !end || (*end != '\0' && *end != '\r')) {
            LOGE ("Некоректна відповідь QM");
            return -1;
        }
        values[count++] = (int)v;
        token = strtok_r (NULL, ",", &save);
    }
    if (count < 3) {
        LOGE ("Некоректна відповідь QM");
        return -1;
    }
    status_out->command_active = values[0];
    status_out->motor1_active = values[1];
    status_out->motor2_active = values[2];
    status_out->fifo_pending = (count >= 4) ? values[3] : 0;
    return 0;
}

/**
 * @brief Виклик `QP` (Query Pen).
 *
 * @param sp          Послідовний порт.
 * @param[out] pen_up_out Прапор стану пера.
 * @param timeout_ms Тайм-аут відповіді.
 */
int ebb_query_pen (serial_port_t *sp, bool *pen_up_out, int timeout_ms) {
    if (!sp || !pen_up_out)
        return -1;

    char line[EBB_RESP_MAX];
    if (ebb_send_query (sp, timeout_ms, line, sizeof (line), "QP") != 0)
        return -1;

    char *end = NULL;
    errno = 0;
    long val = strtol (line, &end, 10);
    if (errno != 0 || !end || (*end != '\0' && *end != '\r')) {
        LOGE ("Некоректна відповідь QP: %s", line);
        return -1;
    }
    *pen_up_out = (val != 0);
    return 0;
}

/**
 * @brief Виклик `QR` (Query Servo Power).
 *
 * @param sp             Послідовний порт.
 * @param[out] power_on_out Прапор живлення серво.
 * @param timeout_ms     Тайм-аут відповіді.
 */
int ebb_query_servo_power (serial_port_t *sp, bool *power_on_out, int timeout_ms) {
    if (!sp || !power_on_out)
        return -1;

    char line[EBB_RESP_MAX];
    if (ebb_send_query (sp, timeout_ms, line, sizeof (line), "QR") != 0)
        return -1;

    char *end = NULL;
    errno = 0;
    long val = strtol (line, &end, 10);
    if (errno != 0 || !end || (*end != '\0' && *end != '\r')) {
        LOGE ("Некоректна відповідь QR: %s", line);
        return -1;
    }
    *power_on_out = (val != 0);
    return 0;
}

/**
 * @brief Отримати версію прошивки (`V`).
 *
 * @param sp          Послідовний порт.
 * @param[out] version_buf Буфер для тексту версії.
 * @param version_len Розмір буфера.
 * @param timeout_ms  Тайм-аут відповіді.
 */
int ebb_query_version (serial_port_t *sp, char *version_buf, size_t version_len, int timeout_ms) {
    if (!sp || !version_buf || version_len == 0)
        return -1;

    char line[EBB_RESP_MAX];
    if (ebb_send_query (sp, timeout_ms, line, sizeof (line), "V") != 0)
        return -1;
    strncpy (version_buf, line, version_len - 1);
    version_buf[version_len - 1] = '\0';
    return 0;
}

/**
 * @brief Зібрати агрегований статус, комбінуючи QM/QS/QP/QR/V.
 *
 * @param sp         Послідовний порт.
 * @param[out] snapshot Буфер результату.
 * @param timeout_ms Тайм-аут для кожного запиту.
 */
int ebb_collect_status (serial_port_t *sp, ebb_status_snapshot_t *snapshot, int timeout_ms) {
    if (!sp || !snapshot)
        return -1;

    memset (snapshot, 0, sizeof (*snapshot));

    if (ebb_query_motion (sp, &snapshot->motion, timeout_ms) != 0)
        return -1;
    if (ebb_query_steps (sp, &snapshot->steps_axis1, &snapshot->steps_axis2, timeout_ms) != 0)
        return -1;
    if (ebb_query_pen (sp, &snapshot->pen_up, timeout_ms) != 0)
        return -1;
    if (ebb_query_servo_power (sp, &snapshot->servo_power, timeout_ms) != 0)
        return -1;
    if (ebb_query_version (sp, snapshot->firmware, sizeof (snapshot->firmware), timeout_ms) != 0)
        return -1;

    return 0;
}

/**
 * @brief Виконати аварійну зупинку (команда `ES`).
 *
 * @param sp         Послідовний порт.
 * @param timeout_ms Тайм-аут відповіді.
 * @return 0 при успіху; -1 у разі збою.
 */
int ebb_emergency_stop (serial_port_t *sp, int timeout_ms) {
    if (!sp)
        return -1;
    return ebb_send_command (sp, timeout_ms, "ES");
}

/**
 * @brief Реалізація команди SC (Stepper/Servo configure).
 *
 * @param sp         Відкритий послідовний порт.
 * @param param_id   Ідентифікатор параметра (0..255).
 * @param value      Значення параметра (0..65535).
 * @param timeout_ms Тайм-аут очікування відповіді OK.
 * @return 0 при успіху; -1 при помилці або некоректних аргументах.
 */
int ebb_configure_mode (serial_port_t *sp, int param_id, int value, int timeout_ms) {
    if (!sp)
        return -1;
    if (param_id < 0 || param_id > 255) {
        LOGE ("Некоректний параметр SC: %d", param_id);
        return -1;
    }
    if (value < 0 || value > 65535) {
        LOGE ("Некоректне значення SC: %d", value);
        return -1;
    }
    return ebb_send_command (sp, timeout_ms, "SC,%d,%d", param_id, value);
}

/**
 * @brief Реалізація команди SR (servo power timeout).
 *
 * @param sp             Відкритий послідовний порт.
 * @param timeout_ms     Тайм-аут у мілісекундах (0 → вимкнути авто-відключення).
 * @param power_state    -1 → не змінювати; 0 → вимкнути; 1 → увімкнути живлення сервоприводу.
 * @param cmd_timeout_ms Тайм-аут очікування відповіді OK.
 * @return 0 при успіху; -1 при помилці/некоректних аргументах.
 */
int ebb_set_servo_power_timeout (
    serial_port_t *sp, uint32_t timeout_ms, int power_state, int cmd_timeout_ms) {
    if (!sp)
        return -1;
    if (power_state == 0 || power_state == 1)
        return ebb_send_command (sp, cmd_timeout_ms, "SR,%u,%d", timeout_ms, power_state);
    return ebb_send_command (sp, cmd_timeout_ms, "SR,%u", timeout_ms);
}
