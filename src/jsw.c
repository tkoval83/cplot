/**
 * @file jsw.c
 * @brief Прості утиліти ЗАПИСУ JSON: екранування рядків та потоковий writer.
 */

#include "jsw.h"

#include <string.h>

/**
 * Екранувати рядок для JSON та вивести у потік.
 *
 * Екрануються символи '"', '\\', керуючі переведення рядка/табуляція тощо.
 * Для байтів < 0x20 виводиться послідовність \u00XX.
 *
 * @param f   Потік виводу.
 * @param s   Вхідний буфер (UTF‑8).
 * @param len Довжина буфера у байтах.
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
                fprintf (f, "\\u%04X", (unsigned)c);
            } else {
                fputc (c, f);
            }
        }
    }
}

/**
 * Вставити кому перед наступним елементом за потреби відповідно до контексту.
 *
 * @param w Записувач JSON.
 */
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

/**
 * Оновити внутрішній стан після виводу значення (лічильники/прапорці).
 *
 * @param w Записувач JSON.
 */
static inline void jsonw_after_value (json_writer_t *w) {
    if (w->depth > 0) {
        unsigned int idx = (unsigned int)(w->depth - 1);
        if (w->type[idx] == 1) {
            w->key_open[idx] = 0;
            w->count[idx]++;
        } else if (w->type[idx] == 2) {
            w->count[idx]++;
        }
    }
}

/**
 * Ініціалізувати записувач JSON для виводу у вказаний потік.
 * @param w Вказівник на структуру (обнуляється).
 * @param f Потік виводу (stdout, файл тощо).
 */
void jsonw_init (json_writer_t *w, FILE *f) {
    memset (w, 0, sizeof (*w));
    w->f = f;
}

/** Почати об’єкт "{" у поточному контексті. */
void jsonw_begin_object (json_writer_t *w) {
    jsonw_maybe_comma (w);
    fputc ('{', w->f);
    w->type[w->depth] = 1;
    w->count[w->depth] = 0;
    w->key_open[w->depth] = 0;
    w->depth++;
}

/** Завершити поточний об’єкт "}". */
void jsonw_end_object (json_writer_t *w) {
    fputc ('}', w->f);
    w->depth--;
    jsonw_after_value (w);
}

/** Почати масив "[" у поточному контексті. */
void jsonw_begin_array (json_writer_t *w) {
    jsonw_maybe_comma (w);
    fputc ('[', w->f);
    w->type[w->depth] = 2;
    w->count[w->depth] = 0;
    w->key_open[w->depth] = 0;
    w->depth++;
}

/** Завершити поточний масив "]". */
void jsonw_end_array (json_writer_t *w) {
    fputc (']', w->f);
    w->depth--;
    jsonw_after_value (w);
}

/**
 * Вивести ключ у поточному об’єкті з двокрапкою: "key":
 * @param w   Записувач.
 * @param key Ключ (UTF‑8).
 */
void jsonw_key (json_writer_t *w, const char *key) {
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

/** Вивести рядок як значення (із екрануванням). */
void jsonw_string (json_writer_t *w, const char *s, size_t len) {
    jsonw_maybe_comma (w);
    fputc ('"', w->f);
    json_fprint_escaped (w->f, s, len);
    fputc ('"', w->f);
    jsonw_after_value (w);
}

/** Зручність: вивести C‑рядок як значення. */
void jsonw_string_cstr (json_writer_t *w, const char *s) { jsonw_string (w, s, strlen (s)); }

/** Вивести булеве значення. */
void jsonw_bool (json_writer_t *w, int b) {
    jsonw_maybe_comma (w);
    fputs (b ? "true" : "false", w->f);
    jsonw_after_value (w);
}

/** Вивести ціле значення. */
void jsonw_int (json_writer_t *w, long long v) {
    jsonw_maybe_comma (w);
    fprintf (w->f, "%lld", v);
    jsonw_after_value (w);
}

/** Вивести число з плаваючою крапкою. */
void jsonw_double (json_writer_t *w, double v) {
    jsonw_maybe_comma (w);
    fprintf (w->f, "%g", v);
    jsonw_after_value (w);
}

/** Вивести null. */
void jsonw_null (json_writer_t *w) {
    jsonw_maybe_comma (w);
    fputs ("null", w->f);
    jsonw_after_value (w);
}

/** Вставити сирий JSON‑фрагмент без екранування. */
void jsonw_raw (json_writer_t *w, const char *raw, size_t len) {
    jsonw_maybe_comma (w);
    fwrite (raw, 1, len, w->f);
    jsonw_after_value (w);
}
