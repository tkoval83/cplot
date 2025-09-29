/**
 * @file str.c
 * @brief Реалізація рядкових утиліт.
 * @ingroup str
 * @details
 * Містить прості операції над ASCII‑рядками та мінімальний UTF‑8 декодер, що
 * перевіряє базову коректність послідовностей і запобігає надмірним формам.
 */

#include "str.h"

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/**
 * @copydoc string_duplicate
 */
int string_duplicate (const char *src, char **out_dst) {
    if (!src || !out_dst)
        return -2;
    size_t len = strlen (src);
    char *copy = (char *)malloc (len + 1);
    if (!copy)
        return -1;
    memcpy (copy, src, len + 1);
    *out_dst = copy;
    return 0;
}

/**
 * @copydoc string_copy
 */
void string_copy (char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0)
        return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy (dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

/**
 * @copydoc string_equals_ci
 */
bool string_equals_ci (const char *a, const char *b) {
    if (!a || !b)
        return false;
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a;
        unsigned char cb = (unsigned char)*b;
        if (tolower (ca) != tolower (cb))
            return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

/**
 * @copydoc string_to_lower_ascii
 */
void string_to_lower_ascii (char *s) {
    if (!s)
        return;
    for (; *s; ++s)
        *s = (char)tolower ((unsigned char)*s);
}

/**
 * @copydoc string_trim_ascii
 */
void string_trim_ascii (char *s) {
    if (!s)
        return;
    char *start = s;
    while (*start && isspace ((unsigned char)*start))
        ++start;
    if (start != s)
        memmove (s, start, strlen (start) + 1);
    char *end = s + strlen (s);
    while (end > s && isspace ((unsigned char)end[-1]))
        --end;
    *end = '\0';
}

/**
 * @copydoc str_utf8_decode
 */
int str_utf8_decode (const char *input, uint32_t *out_cp, size_t *consumed) {
    if (!input || !out_cp)
        return -1;
    const unsigned char *s = (const unsigned char *)input;
    size_t used = 0;
    uint32_t cp = 0;
    if ((s[0] & 0x80) == 0) {
        cp = s[0];
        used = 1;
    } else if ((s[0] & 0xE0) == 0xC0) {
        if ((s[1] & 0xC0) != 0x80)
            return -1;
        cp = ((uint32_t)(s[0] & 0x1F) << 6) | (uint32_t)(s[1] & 0x3F);
        used = 2;
        if (cp < 0x80)
            return -1;
    } else if ((s[0] & 0xF0) == 0xE0) {
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80)
            return -1;
        cp = ((uint32_t)(s[0] & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6)
             | (uint32_t)(s[2] & 0x3F);
        used = 3;
        if (cp < 0x800)
            return -1;
    } else if ((s[0] & 0xF8) == 0xF0) {
        if ((s[1] & 0xC0) != 0x80 || (s[2] & 0xC0) != 0x80 || (s[3] & 0xC0) != 0x80)
            return -1;
        cp = ((uint32_t)(s[0] & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12)
             | ((uint32_t)(s[2] & 0x3F) << 6) | (uint32_t)(s[3] & 0x3F);
        used = 4;
        if (cp < 0x10000)
            return -1;
    } else {
        return -1;
    }
    if (consumed)
        *consumed = used;
    *out_cp = cp;
    return 0;
}
