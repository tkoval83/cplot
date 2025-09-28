#ifndef CPLOT_STR_H
#define CPLOT_STR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Duplicate a NUL-terminated string into heap storage. */
int string_duplicate (const char *src, char **out_dst);

/** Copy `src` into `dst` ensuring a terminating NUL; tolerates NULL `src`. */
void string_copy (char *dst, size_t dst_size, const char *src);

/** Case-insensitive ASCII comparison returning true when both strings match. */
bool string_equals_ci (const char *a, const char *b);

/** Lowercase ASCII characters in place (non-ASCII bytes remain untouched). */
void string_to_lower_ascii (char *s);

/** Trim ASCII whitespace from both ends of the buffer in place. */
void string_trim_ascii (char *s);

/**
 * Декодувати один символ UTF-8 у кодову точку.
 *
 * Поведінка ідентична локальним декодерам у модулях: повертає 0 при успіху
 * і встановлює `*out_cp` та `*consumed` (якщо не NULL) у кількість байтів,
 * що належать поточній кодовій точці. Повертає -1 для некоректних
 * послідовностей або аргументів.
 */
int str_utf8_decode (const char *input, uint32_t *out_cp, size_t *consumed);

#endif /* CPLOT_STR_H */
