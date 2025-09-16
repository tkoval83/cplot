/**
 * @file jsw.h
 * @brief Примітивний ПОПИСУВАЧ JSON (writer) для потокового виводу.
 *
 * Дозволяє послідовно збирати JSON‑структури без ручного керування комами.
 * Підтримуються об’єкти, масиви, ключі та скалярні значення (рядок/число/булеве/null),
 * а також вставка сирого JSON‑фрагмента.
 */
#ifndef CPLOT_JSW_H
#define CPLOT_JSW_H

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Екранувати рядок для безпечного включення у JSON та записати у файл.
 *
 * Функція не додає початкових/кінцевих лапок. Керуючі символи та лапки
 * екрануються згідно стандарту JSON. Unicode‑послідовності не створюються,
 * виводиться буквально для байтів >= 0x20.
 *
 * @param f   Вказівник на відкритий потік виводу.
 * @param s   Вказівник на початок вхідного буфера.
 * @param len Довжина буфера у байтах.
 */
void json_fprint_escaped (FILE *f, const char *s, size_t len);

/**
 * @brief Примітивний потоковий записувач JSON.
 */
typedef struct json_writer {
    FILE *f;                    /**< Потік виводу */
    int depth;                  /**< Поточна глибина вкладення */
    unsigned char type[32];     /**< Стек типів: 1=object, 2=array */
    unsigned int count[32];     /**< Лічильник елементів на кожному рівні */
    unsigned char key_open[32]; /**< Прапорець: у поточному об’єкті очікується значення після key */
} json_writer_t;

/** Ініціалізувати записувач для виводу у вказаний потік. */
void jsonw_init (json_writer_t *w, FILE *f);
/** Почати об’єкт ("{"). */
void jsonw_begin_object (json_writer_t *w);
/** Завершити поточний об’єкт ("}"). */
void jsonw_end_object (json_writer_t *w);
/** Почати масив ("["). */
void jsonw_begin_array (json_writer_t *w);
/** Завершити поточний масив ("]"). */
void jsonw_end_array (json_writer_t *w);

/** Задати ключ у поточному об’єкті: друкує "key": і очікує значення. */
void jsonw_key (json_writer_t *w, const char *key);

/** Надрукувати рядок як значення (із екрануванням). */
void jsonw_string (json_writer_t *w, const char *s, size_t len);
/** Зручність: надрукувати рядок C як значення. */
void jsonw_string_cstr (json_writer_t *w, const char *s);
/** Надрукувати булеве значення. */
void jsonw_bool (json_writer_t *w, int b);
/** Надрукувати ціле значення. */
void jsonw_int (json_writer_t *w, long long v);
/** Надрукувати число з плаваючою крапкою. */
void jsonw_double (json_writer_t *w, double v);
/** Надрукувати null. */
void jsonw_null (json_writer_t *w);
/** Вставити сирий JSON‑фрагмент як значення (без екранування). */
void jsonw_raw (json_writer_t *w, const char *raw, size_t len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CPLOT_JSW_H */
