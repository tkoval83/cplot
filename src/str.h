/**
 * @file str.h
 * @brief Рядкові утиліти: копіювання, порівняння, тримінг, UTF‑8 декодування.
 * @defgroup str Рядки
 * @defgroup util Утиліти
 * Невеликі допоміжні модулі, що не належать до основних підсистем.
 * @details
 * Функції орієнтовані на прості ASCII‑операції та мінімальний UTF‑8 декодер
 * для побайтного розбору. Всі функції безпечні до `NULL` (no‑op або код помилки).
 */
#ifndef CPLOT_STR_H
#define CPLOT_STR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Створює дублікат рядка у новій памʼяті.
 * @param src Джерело (C‑рядок).
 * @param out_dst [out] Вказівник на нову копію (виділяється `malloc`).
 * @return 0 — успіх; -1 — помилка виділення памʼяті; -2 — некоректні аргументи.
 */
int string_duplicate (const char *src, char **out_dst);

/**
 * @brief Безпечна копія рядка з обмеженням розміру й гарантованим `\0`.
 * @param dst [out] Приймач.
 * @param dst_size Розмір буфера `dst` у байтах (>=1).
 * @param src Джерело; якщо `NULL` — у `dst` записується порожній рядок.
 */
void string_copy (char *dst, size_t dst_size, const char *src);

/**
 * @brief Порівняння рядків без урахування регістру (ASCII‑латиниця).
 * @param a Перший рядок.
 * @param b Другий рядок.
 * @return true — рівні; false — різні або `NULL`.
 */
bool string_equals_ci (const char *a, const char *b);

/**
 * @brief Перетворює латинські символи у нижній регістр (ASCII‑латиниця).
 * @param s Рядок для модифікації (in‑place); `NULL` — no‑op.
 */
void string_to_lower_ascii (char *s);

/**
 * @brief Обрізає ASCII‑пробіли з обох кінців.
 * @param s Рядок для модифікації (in‑place); `NULL` — no‑op.
 */
void string_trim_ascii (char *s);

/**
 * @brief Декодує один UTF‑8 символ (мінімальна перевірка коректності).
 * @param input Вказівник на байти UTF‑8 (не `NULL`).
 * @param out_cp [out] Декодована кодова точка Unicode.
 * @param consumed [out] Кількість спожитих байтів (1..4); може бути `NULL`.
 * @return 0 — успіх; -1 — некоректна послідовність.
 * @note Виконується запобігання надмірно довгим формам, але не блокується
 *       декодування деяких не призначених до використання кодових точок.
 */
int str_utf8_decode (const char *input, uint32_t *out_cp, size_t *consumed);

#endif
