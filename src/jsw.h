/**
 * @file jsw.h
 * @brief Примітивний JSON writer: побудова невеликих JSON-рядків.
 * @defgroup jsw JSON Writer
 * @ingroup util
 * @details
 * Легковаговий writer для побудови простих JSON‑обʼєктів у потік `FILE*` без
 * зовнішніх залежностей. Форматує мінімізований JSON (без відступів/переносів)
 * і автоматично керує комами між елементами обʼєктів та масивів.
 */
#ifndef CPLOT_JSW_H
#define CPLOT_JSW_H

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Друкує рядок у файл із екрануванням JSON.
 * @param f Потік виводу (не `NULL`).
 * @param s Рядок джерела (може містити керівні символи).
 * @param len Довжина рядка у байтах.
 * @details Екранує `"`, `\\`, `\n`, `\r`, `\t` та будь‑які символи < 0x20 як `\uXXXX`.
 */
void jsw_json_fprint_escaped (FILE *f, const char *s, size_t len);

/** \brief Максимальна підтримувана глибина вкладеності JSON. */
#define JSW_MAX_DEPTH 32

/**
 * @brief Стан JSON‑writerʼа поверх `FILE*`.
 */
typedef struct json_writer {
    FILE *f;                               /**< Цільовий потік. Власність не передається. */
    int depth;                             /**< Поточна глибина стеку (0 — рівень верхівки). */
    unsigned char type[JSW_MAX_DEPTH];     /**< Типи на стеці: 1 — обʼєкт, 2 — масив. */
    unsigned int count[JSW_MAX_DEPTH];     /**< Кількість елементів у поточному контейнері. */
    unsigned char key_open[JSW_MAX_DEPTH]; /**< Прапорець: очікується значення після ключа. */
} json_writer_t;

/**
 * @brief Ініціалізація writerʼа поверх `FILE*`.
 * @param w [out] Writer, який буде очищено та привʼязано до потоку.
 * @param f Потік виводу (власність лишається у викликувача).
 */
void jsw_jsonw_init (json_writer_t *w, FILE *f);

/**
 * @brief Починає обʼєкт `{}` у поточному контексті.
 * @param w Writer.
 * @note Дозволено викликати на верхньому рівні, у масиві або одразу після `jsw_jsonw_key`.
 */
void jsw_jsonw_begin_object (json_writer_t *w);

/**
 * @brief Завершує поточний обʼєкт `}`.
 * @param w Writer.
 */
void jsw_jsonw_end_object (json_writer_t *w);

/**
 * @brief Починає масив `[` у поточному контексті.
 * @param w Writer.
 */
void jsw_jsonw_begin_array (json_writer_t *w);

/**
 * @brief Завершує поточний масив `]`.
 * @param w Writer.
 */
void jsw_jsonw_end_array (json_writer_t *w);

/**
 * @brief Додає ключ обʼєкта (рядок + двокрапка).
 * @param w Writer.
 * @param key Імʼя ключа (C‑рядок).
 * @note Коректно лише всередині обʼєкта; наступним викликом має бути додавання значення.
 */
void jsw_jsonw_key (json_writer_t *w, const char *key);

/**
 * @brief Додає рядкове значення з явною довжиною.
 * @param w Writer.
 * @param s Вказівник на байти рядка (може містити керівні символи).
 * @param len Довжина у байтах.
 */
void jsw_jsonw_string (json_writer_t *w, const char *s, size_t len);

/**
 * @brief Додає нуль‑термінований C‑рядок як значення.
 * @param w Writer.
 * @param s C‑рядок.
 */
void jsw_jsonw_string_cstr (json_writer_t *w, const char *s);

/**
 * @brief Додає булеве значення.
 * @param w Writer.
 * @param b Нуль/ненуль для false/true.
 */
void jsw_jsonw_bool (json_writer_t *w, int b);

/**
 * @brief Додає ціле число десятковим форматом.
 * @param w Writer.
 * @param v Значення.
 */
void jsw_jsonw_int (json_writer_t *w, long long v);

/**
 * @brief Додає число з плаваючою крапкою (`%g`).
 * @param w Writer.
 * @param v Значення.
 */
void jsw_jsonw_double (json_writer_t *w, double v);

/**
 * @brief Додає значення `null`.
 * @param w Writer.
 */
void jsw_jsonw_null (json_writer_t *w);

/**
 * @brief Додає сирий JSON‑фрагмент без екранування.
 * @param w Writer.
 * @param raw Вказівник на байти JSON‑фрагмента.
 * @param len Довжина у байтах.
 * @warning Вставляється без перевірок/екранування — відповідальність на викликувачеві.
 */
void jsw_jsonw_raw (json_writer_t *w, const char *raw, size_t len);

#ifdef __cplusplus
}
#endif

#endif
