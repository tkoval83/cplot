/**
 * @file trace.h
 * @brief Файлове трасування із обертанням для всіх дій пристрою.
 */
#ifndef CPLOT_TRACE_H
#define CPLOT_TRACE_H

#include "log.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Опції вмикання трасування у файл.
 */
typedef struct trace_options_s {
    const char *path;     /**< Шлях до файлу трасування (NULL → типовий). */
    size_t max_bytes;     /**< Максимальний розмір активного файлу (0 → типовий). */
    unsigned max_files;   /**< Кількість файлів в історії (0 → типовий). */
    log_level_t level;    /**< Мінімальний рівень повідомлень (LOG_DEBUG за замовчуванням). */
} trace_options_t;

/**
 * @brief Увімкнути трасування з опціями обертання файлів.
 *
 * @param options Налаштування трасування (може бути NULL).
 * @return 0 при успіху; -1, якщо файл не вдалося відкрити або створити.
 */
int trace_enable (const trace_options_t *options);

/**
 * @brief Перевірити, чи активовано трасування.
 */
bool trace_is_enabled (void);

/**
 * @brief Записати повідомлення у файл трасування (варіативна форма).
 *
 * @param level Рівень повідомлення (використовується для фільтрації).
 * @param fmt   Форматний рядок (українською), як у printf.
 * @param ap    Список аргументів.
 */
void trace_vwrite (log_level_t level, const char *fmt, va_list ap);

/**
 * @brief Записати повідомлення у файл трасування.
 */
void trace_write (log_level_t level, const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__ ((format (printf, 2, 3)))
#endif
;

/**
 * @brief Вимкнути трасування та закрити файл.
 */
void trace_disable (void);

#ifdef __cplusplus
}
#endif

#endif /* CPLOT_TRACE_H */
