/**
 * @file fontreg.h
 * @brief Реєстр/словник шрифтів Hershey, доступних у дистрибутиві.
 * @defgroup fontreg Реєстр шрифтів
 * @ingroup text
 */
#ifndef FONTREG_H
#define FONTREG_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Опис одного шрифтного обличчя Hershey у реєстрі.
 */
typedef struct {
    char id[64];         /**< Ідентифікатор (стабільний ключ). */
    char name[96];       /**< Людяна назва обличчя. */
    char path[PATH_MAX]; /**< Повний шлях до SVG-файлу. */
} font_face_t;

/**
 * @brief Пара ключ+назва для відображення родини шрифтів.
 */
typedef struct {
    char key[64];  /**< Ключ родини (нижній регістр, без суфіксів стилів). */
    char name[96]; /**< Відображувана назва родини. */
} font_family_name_t;

/**
 * @brief Встановлює кореневу директорію зі шрифтами.
 * @param path Корінь (NULL/порожній — використовувати вбудований шлях).
 */
void fontreg_set_root (const char *path);

/**
 * @brief Повертає список усіх шрифтів.
 * @param faces [out] Масив облич шрифтів (malloc; викликальник звільняє).
 * @param count [out] Кількість елементів.
 * @return 0 — успіх; <0 — помилка/недоступний індекс.
 */
int fontreg_list (font_face_t **faces, size_t *count);

/**
 * @brief Повертає список родин шрифтів.
 * @param families [out] Масив пар ключ/назва (malloc).
 * @param count [out] Кількість.
 * @return 0 — успіх; <0 — помилка.
 */
int fontreg_list_families (font_family_name_t **families, size_t *count);

/**
 * @brief Розвʼязує рядок-запит у конкретне обличчя шрифту.
 * @param query Назва/ідентифікатор/шлях (може бути часткова відповідність).
 * @param out [out] Результат.
 * @return 0 — знайдено; <0 — помилка/не знайдено.
 */
int fontreg_resolve (const char *query, font_face_t *out);

/**
 * @brief Підбирає обличчя, що покриває набір кодових точок.
 * @param preferred_family Бажана родина (може бути NULL/порожня).
 * @param codepoints Набір кодових точок.
 * @param codepoint_count Кількість кодових точок.
 * @param out_face [out] Вибране обличчя.
 * @return 0 — знайдено; <0 — помилка/не знайдено.
 */
int fontreg_select_face_for_codepoints (
    const char *preferred_family,
    const uint32_t *codepoints,
    size_t codepoint_count,
    font_face_t *out_face);

#endif
