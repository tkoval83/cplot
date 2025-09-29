/**
 * @file ebb.c
 * @brief Реалізація команд протоколу EBB.
 * @ingroup ebb
 */

#include "ebb.h"

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

/** Максимальна довжина форматованої команди. */
#define EBB_CMD_MAX 128

/** Максимальна довжина рядка відповіді. */
#define EBB_RESP_MAX 128

/**
 * @brief Форматує та надсилає команду без очікування корисних даних (OK/ERR).
 * @param sp Відкритий серійний порт.
 * @param timeout_ms Тайм-аут читання відповіді (мс).
 * @param fmt Формат керуючої команди EBB.
 * @param ap Аргументи формату (va_list).
 * @return 0 — отримано OK, -1 — помилка.
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
    }

    LOGE ("Контролер не надіслав підтвердження ОК після команди");
    log_print (LOG_ERROR, "контролер: не отримав ОК після '%s'", cmd);
    return -1;
}

/**
 * @brief Зручна обгортка над ebb_send_vcommand з variadic-аргументами.
 */
static int ebb_send_command (serial_port_t *sp, int timeout_ms, const char *fmt, ...) {
    va_list ap;
    va_start (ap, fmt);
    int rc = ebb_send_vcommand (sp, timeout_ms, fmt, ap);
    va_end (ap);
    return rc;
}

/**
 * @brief Форматує та надсилає запит з очікуванням даних + OK.
 * @param sp Серійний порт.
 * @param timeout_ms Тайм-аут (мс).
 * @param resp_out [out] Буфер для даних відповіді (може бути NULL).
 * @param resp_len Розмір буфера відповіді.
 * @param fmt Формат запиту.
 * @param ap va_list аргументів.
 * @return 0 — успіх, -1 — помилка.
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
    }

    LOGE ("Контролер не надіслав підтвердження ОК після запиту");
    log_print (LOG_ERROR, "контролер: не отримав ОК після запиту '%s'", cmd);
    return -1;
}

/**
 * @brief Зручна обгортка над ebb_send_vquery з variadic-аргументами.
 */
static int ebb_send_query (
    serial_port_t *sp, int timeout_ms, char *resp_out, size_t resp_len, const char *fmt, ...) {
    va_list ap;
    va_start (ap, fmt);
    int rc = ebb_send_vquery (sp, timeout_ms, resp_out, resp_len, fmt, ap);
    va_end (ap);
    return rc;
}

/** @copydoc ebb_enable_motors */
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

/** @copydoc ebb_disable_motors */
int ebb_disable_motors (serial_port_t *sp, int timeout_ms) {
    return ebb_enable_motors (sp, EBB_MOTOR_DISABLED, EBB_MOTOR_DISABLED, timeout_ms);
}

/** @copydoc ebb_move_steps */
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

/** @copydoc ebb_pen_set */
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

/** @copydoc ebb_move_mixed */
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

/** Перевіряє допустимість прапорців очищення. */
static int ebb_validate_clear_flags (int clear_flags) {
    return (clear_flags == -1) || (clear_flags >= EBB_CLEAR_NONE && clear_flags <= EBB_CLEAR_BOTH);
}

/** @copydoc ebb_move_lowlevel_steps */
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

/** @copydoc ebb_move_lowlevel_time */
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

/** Максимальна абсолютна позиція для HM (запобігає переповненню). */
#define EBB_HOME_MAX_POSITION 4294967

/** @copydoc ebb_home_move */
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

/** @copydoc ebb_query_steps */
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

/** @copydoc ebb_clear_steps */
int ebb_clear_steps (serial_port_t *sp, int timeout_ms) {
    if (!sp)
        return -1;
    return ebb_send_command (sp, timeout_ms, "CS");
}

/** @copydoc ebb_query_motion */
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

/** @copydoc ebb_query_pen */
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

/** @copydoc ebb_query_servo_power */
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

/** @copydoc ebb_query_version */
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

/** @copydoc ebb_emergency_stop */
int ebb_emergency_stop (serial_port_t *sp, int timeout_ms) {
    if (!sp)
        return -1;
    return ebb_send_command (sp, timeout_ms, "ES");
}

/** @copydoc ebb_configure_mode */
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

/** @copydoc ebb_set_servo_power_timeout */
int ebb_set_servo_power_timeout (
    serial_port_t *sp, uint32_t timeout_ms, int power_state, int cmd_timeout_ms) {
    if (!sp)
        return -1;
    if (power_state == 0 || power_state == 1)
        return ebb_send_command (sp, cmd_timeout_ms, "SR,%u,%d", timeout_ms, power_state);
    return ebb_send_command (sp, cmd_timeout_ms, "SR,%u", timeout_ms);
}
