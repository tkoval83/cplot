/**
 * @file jsr.h
 * @brief Примітивний JSON reader: пошук ключів, отримання значень.
 * @defgroup jsr JSON Reader
 * @ingroup util
 * @details
 * Легковаговий читач JSON без зовнішніх залежностей. Підтримує:
 * - пошук значення за ключем верхнього рівня;
 * - повернення "сирого" значення (вказівник + довжина підрядка);
 * - розбір простих типів: рядок (з частковою обробкою ескейпів), булеве, число.
 *
 * Обмеження: не виконує повну валідацію JSON, не підтримує повністю `\uXXXX`
 * (підставляє `?`), не розбирає вкладені обʼєкти в глибину та припускає унікальні
 * ключі верхнього рівня. Призначений для невеликих конфігурацій.
 */
#ifndef CPLOT_JSR_H
#define CPLOT_JSR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Пропускає пробіли та повертає перший непробільний символ.
 * @param p Вхідний вказівник у JSON-рядку (не `NULL`).
 * @return Вказівник на перший непробільний символ (може вказувати на `\0`).
 */
const char *jsr_json_skip_ws (const char *p);

/**
 * @brief Знаходить початок значення за ключем верхнього рівня.
 * @param json Повний JSON-документ (C‑рядок; не `NULL`).
 * @param key Ключ верхнього рівня (без лапок; не `NULL`).
 * @return Вказівник на перший символ значення після двокрапки, пропустивши
 * пробіли; або `NULL`, якщо ключ не знайдено.
 */
const char *jsr_json_find_value (const char *json, const char *key);

/**
 * @brief Повертає сире значення (вказівник + довжина) за ключем.
 * @param json JSON‑рядок (не `NULL`).
 * @param key Ключ верхнього рівня (не `NULL`).
 * @param out_ptr [out] Початок значення (всередині `json`).
 * @param out_len [out] Довжина значення у байтах.
 * @return 1 — знайдено; 0 — не знайдено/помилка аргументів.
 * @warning Повертає підрядок, що посилається на вихідний буфер `json`.
 */
int jsr_json_get_raw (const char *json, const char *key, const char **out_ptr, size_t *out_len);

/**
 * @brief Копіює рядкове значення у нову памʼять (завершене `\0`).
 * @param json JSON‑рядок (не `NULL`).
 * @param key Ключ верхнього рівня (не `NULL`).
 * @param out_len [out] Якщо не `NULL` — довжина рядка без термінатора.
 * @return Новий рядок, який належить викликачеві (потрібно `free()`), або `NULL`.
 * @note Підтримуються екскейпи: `\n`, `\t`, `\r`, `\\`, `\"`, `\/`; `\uXXXX`
 * спрощено, символ замінюється на `?`.
 */
char *jsr_json_get_string (const char *json, const char *key, size_t *out_len);

/**
 * @brief Зчитує булеве значення, повертає `defval`, якщо ключ відсутній або некоректний.
 * @param json JSON‑рядок.
 * @param key Ключ верхнього рівня.
 * @param defval Значення за замовчуванням.
 * @return 1 для `true`, 0 для `false`, інакше `defval`.
 */
int jsr_json_get_bool (const char *json, const char *key, int defval);

/**
 * @brief Зчитує число з плаваючою крапкою; у разі помилки повертає `defval`.
 * @param json JSON‑рядок.
 * @param key Ключ верхнього рівня.
 * @param defval Значення за замовчуванням.
 * @return Подвійна точність з `strtod` або `defval`.
 */
double jsr_json_get_double (const char *json, const char *key, double defval);

#ifdef __cplusplus
}
#endif

#endif
