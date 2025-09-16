/**
 * @file json.c
 * @brief Мінімальні утиліти JSON: розбір ключових значень та екранування рядків.
 *
 * Реалізація спрощеного API для типових задач:
 * - пошук значення за іменем ключа (json_find_value);
 * - отримання фрагмента значення без копіювання (json_get_raw);
 * - отримання рядка з розекрануванням (json_get_string);
 * - читання булевого та числового значення (json_get_bool/json_get_double);
 * - безпечне екранування довільного рядка для JSON (json_fprint_escaped).
 *
 * Обмеження див. у заголовку json.h.
 */

#include "json.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/**
 * Пропустити пробільні символи (space, tab, CR, LF).
 *
 * @param p Вказівник на початок ділянки.
 * @return Вказівник на перший непробільний символ.
 */
const char *json_skip_ws (const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    return p;
}

/**
 * Знайти значення за ключем у JSON‑рядку.
 *
 * Повертає вказівник на початок значення (після ':', пропуски пропускаються).
 * Якщо ключ не знайдено — повертає NULL.
 *
 * @param json Вхідний JSON‑рядок.
 * @param key  Ім’я ключа без лапок.
 */
const char *json_find_value (const char *json, const char *key) {
    size_t klen = strlen (key);
    const char *p = json;
    while ((p = strstr (p, "\"")) != NULL) {
        p++;
        if (strncmp (p, key, klen) == 0 && p[klen] == '\"') {
            p += klen + 1;
            p = json_skip_ws (p);
            if (*p == ':') {
                p++;
                return json_skip_ws (p);
            }
        }
    }
    return NULL;
}

/**
 * Отримати сирий фрагмент значення для ключа.
 *
 * Функція враховує вкладені дужки {} та [] та рядкові літерали, щоб знайти межу значення.
 *
 * @param json    Вхідний JSON‑рядок.
 * @param key     Ім’я ключа без лапок.
 * @param out_ptr Вихід: початок фрагмента.
 * @param out_len Вихід: довжина фрагмента.
 * @return 1 при успіху, 0 якщо ключ не знайдено.
 */
int json_get_raw (const char *json, const char *key, const char **out_ptr, size_t *out_len) {
    const char *v = json_find_value (json, key);
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
 * Отримати рядок (значення у лапках) за ключем і розекранувати послідовності.
 *
 * @param json    Вхідний JSON‑рядок.
 * @param key     Ім’я ключа без лапок.
 * @param out_len Вихід: довжина отриманого рядка (без NUL), може бути NULL.
 * @return Новий буфер або NULL при помилці/відсутності ключа/нерядковому значенні.
 */
char *json_get_string (const char *json, const char *key, size_t *out_len) {
    const char *v = json_find_value (json, key);
    if (!v || *v != '"')
        return NULL;
    v++;
    const char *p = v;
    size_t cap = 64, len = 0;
    char *buf = (char *)malloc (cap);
    if (!buf)
        return NULL;
    while (*p) {
        if (*p == '\\') {
            p++;
            if (*p == 'n') {
                if (len + 1 >= cap) {
                    cap *= 2;
                    buf = (char *)realloc (buf, cap);
                    if (!buf)
                        return NULL;
                }
                buf[len++] = '\n';
                p++;
                continue;
            }
            if (*p == 't') {
                if (len + 1 >= cap) {
                    cap *= 2;
                    buf = (char *)realloc (buf, cap);
                    if (!buf)
                        return NULL;
                }
                buf[len++] = '\t';
                p++;
                continue;
            }
            if (*p == 'r') {
                if (len + 1 >= cap) {
                    cap *= 2;
                    buf = (char *)realloc (buf, cap);
                    if (!buf)
                        return NULL;
                }
                buf[len++] = '\r';
                p++;
                continue;
            }
            if (*p == '"' || *p == '\\' || *p == '/') {
                if (len + 1 >= cap) {
                    cap *= 2;
                    buf = (char *)realloc (buf, cap);
                    if (!buf)
                        return NULL;
                }
                buf[len++] = *p++;
                continue;
            }
            if (*p == 'u') {
                p++;
                for (int i = 0; i < 4 && isxdigit ((unsigned char)p[i]); i++) { /* skip */
                }
                p += 4;
                if (len + 1 >= cap) {
                    cap *= 2;
                    buf = (char *)realloc (buf, cap);
                    if (!buf)
                        return NULL;
                }
                buf[len++] = '?';
                continue;
            }
            if (len + 1 >= cap) {
                cap *= 2;
                buf = (char *)realloc (buf, cap);
                if (!buf)
                    return NULL;
            }
            buf[len++] = *p++;
            continue;
        }
        if (*p == '"') {
            p++;
            break;
        }
        if (len + 1 >= cap) {
            cap *= 2;
            buf = (char *)realloc (buf, cap);
            if (!buf)
                return NULL;
        }
        buf[len++] = *p++;
    }
    if (out_len)
        *out_len = len;
    if (len == cap) {
        cap++;
        buf = (char *)realloc (buf, cap);
        if (!buf)
            return NULL;
    }
    buf[len] = '\0';
    return buf;
}

/**
 * Прочитати булеве значення (true/false) або повернути типовий defval.
 */
int json_get_bool (const char *json, const char *key, int defval) {
    const char *v = json_find_value (json, key);
    if (!v)
        return defval;
    if (strncmp (v, "true", 4) == 0)
        return 1;
    if (strncmp (v, "false", 5) == 0)
        return 0;
    return defval;
}

/**
 * Прочитати дійсне число (double) або повернути типовий defval при збої розбору.
 */
double json_get_double (const char *json, const char *key, double defval) {
    const char *v = json_find_value (json, key);
    if (!v)
        return defval;
    char *end;
    double d = strtod (v, &end);
    if (end == v)
        return defval;
    return d;
}

/**
 * Екранувати рядок для JSON та надрукувати у вказаний потік.
 */
void json_fprint_escaped (FILE *f, const char *s, size_t len) {
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"':
            fputs ("\\\"", f);
            break;
        case '\\':
            fputs ("\\\\", f);
            break;
        case '\n':
            fputs ("\\n", f);
            break;
        case '\r':
            fputs ("\\r", f);
            break;
        case '\t':
            fputs ("\\t", f);
            break;
        default:
            if (c < 0x20) {
                /* керуючі символи як \u00XX */
                fprintf (f, "\\u%04X", (unsigned)c);
            } else {
                fputc (c, f);
            }
        }
    }
}

/* ---- JSON writer ------------------------------------------------------- */

static inline void jsonw_maybe_comma (json_writer_t *w) {
    if (w->depth > 0) {
        unsigned int idx = (unsigned int)(w->depth - 1);
        if (w->type[idx] == 1 /* object */) {
            if (w->key_open[idx]) {
                /* очікується значення після ключа, кома не потрібна */
            } else if (w->count[idx] > 0) {
                fputc (',', w->f);
            }
        } else if (w->type[idx] == 2 /* array */) {
            if (w->count[idx] > 0)
                fputc (',', w->f);
        }
    }
}

static inline void jsonw_after_value (json_writer_t *w) {
    if (w->depth > 0) {
        unsigned int idx = (unsigned int)(w->depth - 1);
        if (w->type[idx] == 1) {
            /* закрили пару key:value */
            w->key_open[idx] = 0;
            w->count[idx]++;
        } else if (w->type[idx] == 2) {
            w->count[idx]++;
        }
    }
}

void jsonw_init (json_writer_t *w, FILE *f) {
    memset (w, 0, sizeof (*w));
    w->f = f;
}

void jsonw_begin_object (json_writer_t *w) {
    jsonw_maybe_comma (w);
    fputc ('{', w->f);
    w->type[w->depth] = 1;
    w->count[w->depth] = 0;
    w->key_open[w->depth] = 0;
    w->depth++;
}

void jsonw_end_object (json_writer_t *w) {
    fputc ('}', w->f);
    w->depth--;
    jsonw_after_value (w);
}

void jsonw_begin_array (json_writer_t *w) {
    jsonw_maybe_comma (w);
    fputc ('[', w->f);
    w->type[w->depth] = 2;
    w->count[w->depth] = 0;
    w->key_open[w->depth] = 0;
    w->depth++;
}

void jsonw_end_array (json_writer_t *w) {
    fputc (']', w->f);
    w->depth--;
    jsonw_after_value (w);
}

void jsonw_key (json_writer_t *w, const char *key) {
    /* вставити кому між парами якщо потрібно */
    if (w->depth > 0) {
        unsigned int idx = (unsigned int)(w->depth - 1);
        if (w->type[idx] == 1) {
            if (w->count[idx] > 0)
                fputc (',', w->f);
        }
    }
    fputc ('"', w->f);
    json_fprint_escaped (w->f, key, strlen (key));
    fputc ('"', w->f);
    fputc (':', w->f);
    if (w->depth > 0) {
        w->key_open[w->depth - 1] = 1;
    }
}

void jsonw_string (json_writer_t *w, const char *s, size_t len) {
    jsonw_maybe_comma (w);
    fputc ('"', w->f);
    json_fprint_escaped (w->f, s, len);
    fputc ('"', w->f);
    jsonw_after_value (w);
}

void jsonw_string_cstr (json_writer_t *w, const char *s) { jsonw_string (w, s, strlen (s)); }

void jsonw_bool (json_writer_t *w, int b) {
    jsonw_maybe_comma (w);
    fputs (b ? "true" : "false", w->f);
    jsonw_after_value (w);
}

void jsonw_int (json_writer_t *w, long long v) {
    jsonw_maybe_comma (w);
    fprintf (w->f, "%lld", v);
    jsonw_after_value (w);
}

void jsonw_double (json_writer_t *w, double v) {
    jsonw_maybe_comma (w);
    /* достатньо для службових чисел */
    fprintf (w->f, "%g", v);
    jsonw_after_value (w);
}

void jsonw_null (json_writer_t *w) {
    jsonw_maybe_comma (w);
    fputs ("null", w->f);
    jsonw_after_value (w);
}

void jsonw_raw (json_writer_t *w, const char *raw, size_t len) {
    jsonw_maybe_comma (w);
    fwrite (raw, 1, len, w->f);
    jsonw_after_value (w);
}
