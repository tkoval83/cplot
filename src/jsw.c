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
 * @copydoc jsw_json_fprint_escaped
 */
void jsw_json_fprint_escaped (FILE *f, const char *s, size_t len) {
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
static inline void jsw_jsonw_maybe_comma (json_writer_t *w) {
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
static inline void jsw_jsonw_after_value (json_writer_t *w) {
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
 * @copydoc jsw_jsonw_init
 */
void jsw_jsonw_init (json_writer_t *w, FILE *f) {
    memset (w, 0, sizeof (*w));
    w->f = f;
}

/**
 * @copydoc jsw_jsonw_begin_object
 */
void jsw_jsonw_begin_object (json_writer_t *w) {
    jsw_jsonw_maybe_comma (w);
    fputc ('{', w->f);
    w->type[w->depth] = JSW_CTX_OBJECT;
    w->count[w->depth] = 0;
    w->key_open[w->depth] = 0;
    w->depth++;
}

/**
 * @copydoc jsw_jsonw_end_object
 */
void jsw_jsonw_end_object (json_writer_t *w) {
    fputc ('}', w->f);
    w->depth--;
    jsw_jsonw_after_value (w);
}

/**
 * @copydoc jsw_jsonw_begin_array
 */
void jsw_jsonw_begin_array (json_writer_t *w) {
    jsw_jsonw_maybe_comma (w);
    fputc ('[', w->f);
    w->type[w->depth] = JSW_CTX_ARRAY;
    w->count[w->depth] = 0;
    w->key_open[w->depth] = 0;
    w->depth++;
}

/**
 * @copydoc jsw_jsonw_end_array
 */
void jsw_jsonw_end_array (json_writer_t *w) {
    fputc (']', w->f);
    w->depth--;
    jsw_jsonw_after_value (w);
}

/**
 * @copydoc jsw_jsonw_key
 */
void jsw_jsonw_key (json_writer_t *w, const char *key) {
    if (w->depth > 0) {
        unsigned int idx = (unsigned int)(w->depth - 1);
        if (w->type[idx] == JSW_CTX_OBJECT) {
            if (w->count[idx] > 0)
                fputc (',', w->f);
        }
    }
    fputc ('"', w->f);
    jsw_json_fprint_escaped (w->f, key, strlen (key));
    fputc ('"', w->f);
    fputc (':', w->f);
    if (w->depth > 0) {
        w->key_open[w->depth - 1] = 1;
    }
}

/**
 * @copydoc jsw_jsonw_string
 */
void jsw_jsonw_string (json_writer_t *w, const char *s, size_t len) {
    jsw_jsonw_maybe_comma (w);
    fputc ('"', w->f);
    jsw_json_fprint_escaped (w->f, s, len);
    fputc ('"', w->f);
    jsw_jsonw_after_value (w);
}

/**
 * @copydoc jsw_jsonw_string_cstr
 */
void jsw_jsonw_string_cstr (json_writer_t *w, const char *s) { jsw_jsonw_string (w, s, strlen (s)); }

/**
 * @copydoc jsw_jsonw_bool
 */
void jsw_jsonw_bool (json_writer_t *w, int b) {
    jsw_jsonw_maybe_comma (w);
    fputs (b ? "true" : "false", w->f);
    jsw_jsonw_after_value (w);
}

/**
 * @copydoc jsw_jsonw_int
 */
void jsw_jsonw_int (json_writer_t *w, long long v) {
    jsw_jsonw_maybe_comma (w);
    fprintf (w->f, "%lld", v);
    jsw_jsonw_after_value (w);
}

/**
 * @copydoc jsw_jsonw_double
 */
void jsw_jsonw_double (json_writer_t *w, double v) {
    jsw_jsonw_maybe_comma (w);
    fprintf (w->f, "%g", v);
    jsw_jsonw_after_value (w);
}

/**
 * @copydoc jsw_jsonw_null
 */
void jsw_jsonw_null (json_writer_t *w) {
    jsw_jsonw_maybe_comma (w);
    fputs ("null", w->f);
    jsw_jsonw_after_value (w);
}

/**
 * @copydoc jsw_jsonw_raw
 */
void jsw_jsonw_raw (json_writer_t *w, const char *raw, size_t len) {
    jsw_jsonw_maybe_comma (w);
    fwrite (raw, 1, len, w->f);
    jsw_jsonw_after_value (w);
}
