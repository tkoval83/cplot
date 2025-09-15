/**
 * @file fontreg.h
 * @brief Утиліти реєстру шрифтів Hershey (вбудовані шрифти у каталозі hershey/).
 */
#ifndef FONTREG_H
#define FONTREG_H

#include <stddef.h>

/// Гарнітура Hershey, як у hershey/index.json.
typedef struct {
    char id[64];    /**< stable key from index.json */
    char name[96];  /**< human-friendly name */
    char path[128]; /**< relative file path under hershey/ */
} font_face_t;

/**
 * Завантажити реєстр шрифтів Hershey з hershey/index.json.
 * Викликаючий код має звільнити масив через free().
 * @param faces Вихідний вказівник на новий масив гарнітур.
 * @param count Кількість елементів у масиві.
 * @return 0 у разі успіху; ненульове при помилці.
 */
int fontreg_list (font_face_t **faces, size_t *count);

/**
 * Знайти шрифт за запитом (без врахування регістру; збігається з id або підрядком імені).
 * Якщо query NULL/порожній — повернути типову гарнітуру (Hershey Sans Med).
 * @param query Id або ім'я (можливий підрядок), без врахування регістру.
 * @param out   Вихідний знайдений шрифт.
 * @return 0 у разі успіху; ненульове, якщо не знайдено або при помилці.
 */
int fontreg_resolve (const char *query, font_face_t *out);

#endif /* FONTREG_H */
