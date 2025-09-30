/**
 * @file jsr.c
 * @brief Реалізація простого читача JSON.
 * @ingroup jsr
 * @details
 * Мінімалістична реалізація пошуку та вилучення значень за ключами верхнього
 * рівня без повної перевірки синтаксису JSON. Придатна для невеликих конфігів.
 */

#include "jsr.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/**
 * @copydoc jsr_json_skip_ws
 */
const char *jsr_json_skip_ws (const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    return p;
}

/**
 * @copydoc jsr_json_find_value
 */
const char *jsr_json_find_value (const char *json, const char *key) {
    size_t klen = strlen (key);
    const char *p = json;
    while ((p = strstr (p, "\"")) != NULL) {
        p++;
        if (strncmp (p, key, klen) == 0 && p[klen] == '\"') {
            p += klen + 1;
            p = jsr_json_skip_ws (p);
            if (*p == ':') {
                p++;
                return jsr_json_skip_ws (p);
            }
        }
    }
    return NULL;
}

/**
 * @copydoc jsr_json_get_raw
 */
int jsr_json_get_raw (const char *json, const char *key, const char **out_ptr, size_t *out_len) {
    const char *v = jsr_json_find_value (json, key);
    if (!v)
        return 0;
    const char *p = v;
    int depth = 0;
    int in_string = 0;
    while (*p) {
        if (!in_string) {
            if (*p == '"') {
                in_string = 1;
                p++;
                continue;
            }
            if (*p == '{' || *p == '[')
                depth++;
            else if (*p == '}' || *p == ']') {
                if (depth == 0)
                    break;
                depth--;
            } else if (*p == ',' && depth == 0) {
                break;
            }
        } else {
            if (*p == '\\' && p[1]) {
                p += 2;
                continue;
            }
            if (*p == '"')
                in_string = 0;
        }
        p++;
    }
    const char *end = p;
    while (end > v && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
        end--;
    *out_ptr = v;
    *out_len = (size_t)(end - v);
    return 1;
}

/**
 * @copydoc jsr_json_get_string
 */
char *jsr_json_get_string (const char *json, const char *key, size_t *out_len) {
    const char *v = jsr_json_find_value (json, key);
    if (!v || *v != '"')
        return NULL;
    v++;
    const char *p = v;
    size_t cap = 64;
    size_t len = 0;
    char *buf = (char *)malloc (cap);
    if (!buf)
        return NULL;

/**
 * \brief Локальний макрос гарантує достатню ємність буфера `buf`.
 * @param extra Скільки додаткових байтів потрібно вмістити.
 */
#define ENSURE_CAP(extra)                                                                          \
    do {                                                                                           \
        if (len + (extra) >= cap) {                                                                \
            size_t new_cap = cap ? cap : 64;                                                       \
            while (len + (extra) >= new_cap)                                                       \
                new_cap *= 2;                                                                      \
            char *tmp = (char *)realloc (buf, new_cap);                                            \
            if (!tmp)                                                                              \
                goto fail;                                                                         \
            buf = tmp;                                                                             \
            cap = new_cap;                                                                         \
        }                                                                                          \
    } while (0)

    while (*p) {
        if (*p == '\\') {
            p++;
            if (*p == '\0')
                break;
            if (*p == 'n') {
                ENSURE_CAP (1);
                buf[len++] = '\n';
                p++;
                continue;
            }
            if (*p == 't') {
                ENSURE_CAP (1);
                buf[len++] = '\t';
                p++;
                continue;
            }
            if (*p == 'r') {
                ENSURE_CAP (1);
                buf[len++] = '\r';
                p++;
                continue;
            }
            if (*p == '"' || *p == '\\' || *p == '/') {
                ENSURE_CAP (1);
                buf[len++] = *p++;
                continue;
            }
            if (*p == 'u') {
                p++;
                int digits = 0;
                while (digits < 4 && p[digits] && isxdigit ((unsigned char)p[digits]))
                    digits++;
                p += digits;
                ENSURE_CAP (1);
                buf[len++] = '?';
                continue;
            }
            ENSURE_CAP (1);
            buf[len++] = *p++;
            continue;
        }
        if (*p == '"') {
            p++;
            break;
        }
        ENSURE_CAP (1);
        buf[len++] = *p++;
    }
    if (out_len)
        *out_len = len;
    ENSURE_CAP (1);
    buf[len] = '\0';
#undef ENSURE_CAP
    return buf;

fail:
    free (buf);
    return NULL;
}

/**
 * @copydoc jsr_json_get_bool
 */
int jsr_json_get_bool (const char *json, const char *key, int defval) {
    const char *v = jsr_json_find_value (json, key);
    if (!v)
        return defval;
    if (strncmp (v, "true", 4) == 0)
        return 1;
    if (strncmp (v, "false", 5) == 0)
        return 0;
    return defval;
}

/**
 * @copydoc jsr_json_get_double
 */
double jsr_json_get_double (const char *json, const char *key, double defval) {
    const char *v = jsr_json_find_value (json, key);
    if (!v)
        return defval;
    char *end;
    double d = strtod (v, &end);
    if (end == v)
        return defval;
    return d;
}
