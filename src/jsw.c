/**
 * @file jsw.c
 * @brief Реалізація простого JSON writer.
 * @ingroup jsw
 * @details
 * Реалізує запис мінімізованого JSON у `FILE*`, автоматично розставляючи коми
 * між елементами контейнерів та керуючи станом вкладеності.
 */

#include "jsw.h"

#include <string.h>

/**
 * @copydoc json_fprint_escaped
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

/** \brief Вставляє кому за потреби перед новим елементом. */
static inline void jsonw_maybe_comma (json_writer_t *w) {
    if (w->depth > 0) {
        unsigned int idx = (unsigned int)(w->depth - 1);
        if (w->type[idx] == 1) { /* обʼєкт */
            if (w->key_open[idx]) {

            } else if (w->count[idx] > 0) {
                fputc (',', w->f);
            }
        } else if (w->type[idx] == 2) { /* масив */
            if (w->count[idx] > 0)
                fputc (',', w->f);
        }
    }
}

/** \brief Оновлює лічильники після запису значення. */
static inline void jsonw_after_value (json_writer_t *w) {
    if (w->depth > 0) {
        unsigned int idx = (unsigned int)(w->depth - 1);
        if (w->type[idx] == 1) { /* обʼєкт */
            w->key_open[idx] = 0;
            w->count[idx]++;
        } else if (w->type[idx] == 2) { /* масив */
            w->count[idx]++;
        }
    }
}

/** \brief Код типу контексту на стеці: обʼєкт. */
#define JSW_CTX_OBJECT 1
/** \brief Код типу контексту на стеці: масив. */
#define JSW_CTX_ARRAY 2

/**
 * @copydoc jsonw_init
 */
void jsonw_init (json_writer_t *w, FILE *f) {
    memset (w, 0, sizeof (*w));
    w->f = f;
}

/**
 * @copydoc jsonw_begin_object
 */
void jsonw_begin_object (json_writer_t *w) {
    jsonw_maybe_comma (w);
    fputc ('{', w->f);
    w->type[w->depth] = JSW_CTX_OBJECT;
    w->count[w->depth] = 0;
    w->key_open[w->depth] = 0;
    w->depth++;
}

/**
 * @copydoc jsonw_end_object
 */
void jsonw_end_object (json_writer_t *w) {
    fputc ('}', w->f);
    w->depth--;
    jsonw_after_value (w);
}

/**
 * @copydoc jsonw_begin_array
 */
void jsonw_begin_array (json_writer_t *w) {
    jsonw_maybe_comma (w);
    fputc ('[', w->f);
    w->type[w->depth] = JSW_CTX_ARRAY;
    w->count[w->depth] = 0;
    w->key_open[w->depth] = 0;
    w->depth++;
}

/**
 * @copydoc jsonw_end_array
 */
void jsonw_end_array (json_writer_t *w) {
    fputc (']', w->f);
    w->depth--;
    jsonw_after_value (w);
}

/**
 * @copydoc jsonw_key
 */
void jsonw_key (json_writer_t *w, const char *key) {
    if (w->depth > 0) {
        unsigned int idx = (unsigned int)(w->depth - 1);
        if (w->type[idx] == JSW_CTX_OBJECT) {
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

/**
 * @copydoc jsonw_string
 */
void jsonw_string (json_writer_t *w, const char *s, size_t len) {
    jsonw_maybe_comma (w);
    fputc ('"', w->f);
    json_fprint_escaped (w->f, s, len);
    fputc ('"', w->f);
    jsonw_after_value (w);
}

/**
 * @copydoc jsonw_string_cstr
 */
void jsonw_string_cstr (json_writer_t *w, const char *s) { jsonw_string (w, s, strlen (s)); }

/**
 * @copydoc jsonw_bool
 */
void jsonw_bool (json_writer_t *w, int b) {
    jsonw_maybe_comma (w);
    fputs (b ? "true" : "false", w->f);
    jsonw_after_value (w);
}

/**
 * @copydoc jsonw_int
 */
void jsonw_int (json_writer_t *w, long long v) {
    jsonw_maybe_comma (w);
    fprintf (w->f, "%lld", v);
    jsonw_after_value (w);
}

/**
 * @copydoc jsonw_double
 */
void jsonw_double (json_writer_t *w, double v) {
    jsonw_maybe_comma (w);
    fprintf (w->f, "%g", v);
    jsonw_after_value (w);
}

/**
 * @copydoc jsonw_null
 */
void jsonw_null (json_writer_t *w) {
    jsonw_maybe_comma (w);
    fputs ("null", w->f);
    jsonw_after_value (w);
}

/**
 * @copydoc jsonw_raw
 */
void jsonw_raw (json_writer_t *w, const char *raw, size_t len) {
    jsonw_maybe_comma (w);
    fwrite (raw, 1, len, w->f);
    jsonw_after_value (w);
}
