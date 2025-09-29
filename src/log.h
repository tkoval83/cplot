/**
 * @file log.h
 * @brief Мінімалістичне журналювання українською з керуванням кольорами.
 * @defgroup log Журнали
 * @details
 * Легкий модуль журналювання для stderr із рівнями (error/warn/info/debug) та
 * опціональними ANSI‑кольорами. Керування здійснюється через API або змінні
 * середовища `CPLOT_LOG` і `CPLOT_LOG_NO_COLOR`.
 */
#ifndef LOG_H
#define LOG_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

/** \brief ANSI‑послідовність для червоного кольору. */
#ifndef RED
#define RED "\x1b[31m"
#endif
/** \brief ANSI‑послідовність для жовто‑коричневого (warning). */
#ifndef BROWN
#define BROWN "\x1b[33m"
#endif
/** \brief ANSI‑послідовність для синього (info). */
#ifndef BLUE
#define BLUE "\x1b[34m"
#endif
/** \brief ANSI‑послідовність для сірого (debug). */
#ifndef GRAY
#define GRAY "\x1b[90m"
#endif
/** \brief ANSI‑послідовність для скидання кольорів. */
#ifndef NO_COLOR
#define NO_COLOR "\x1b[0m"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Рівні журналювання (нижчий номер — вищий пріоритет).
 */
typedef enum {
    LOG_ERROR = 0, /**< Критичні помилки, що переривають роботу. */
    LOG_WARN = 1,  /**< Попередження про нетипові ситуації. */
    LOG_INFO = 2,  /**< Інформаційні повідомлення за замовчуванням. */
    LOG_DEBUG = 3, /**< Діагностика для налагодження. */
} log_level_t;

/**
 * Конфігурація журналювання (глобальна для процесу).
 */
typedef struct log_config_s {
    log_level_t level; /**< Поточний поріг рівня (виводяться <= цього рівня). */
    bool use_colors;   /**< Чи використовувати ANSI‑кольори (ігнорується, якщо `stderr` не TTY). */
} log_config_t;

/**
 * @brief Повертає покажчик на глобальну конфігурацію.
 * @return Вказівник на внутрішню структуру (не звільняти).
 * @warning Не потокобезпечно. Зміни застосовуються негайно для всіх потоків.
 */
log_config_t *log_get_config (void);

/**
 * @brief Встановлює поріг рівня журналювання.
 * @param level Рівень (виводитимуться повідомлення з рівнем <= `level`).
 */
void log_set_level (log_level_t level);

/**
 * @brief Вмикає/вимикає використання ANSI‑кольорів.
 * @param use `true` — запит на використання кольорів; `false` — примусово вимкнути.
 * @details Якщо `use == true`, кольори будуть застосовані лише якщо `stderr` є TTY.
 */
void log_set_use_colors (bool use);

/**
 * @brief Ініціалізує конфігурацію зі змінних середовища.
 * @details
 * - `CPLOT_LOG` — один із: `debug`, `info`, `warn`(`warning`), `error`.
 * - `CPLOT_LOG_NO_COLOR` — якщо `1`/`true`/`yes`, вимикає кольори.
 * Інакше кольори визначаються `isatty(stderr)` та `log_set_use_colors`.
 * @return 0 — успіх.
 */
int log_init_from_env (void);

/**
 * @brief Друкує повідомлення у `stderr` за допомогою `va_list`.
 * @param level Рівень повідомлення.
 * @param fmt Форматний рядок `printf` українською.
 * @param ap Список аргументів.
 * @note Додає перевід рядка наприкінці. Ігнорує повідомлення вище поточного порога.
 */
void log_vprint (log_level_t level, const char *fmt, va_list ap);
#if defined(__GNUC__) || defined(__clang__)
__attribute__ ((format (printf, 2, 3)))
#endif
/**
 * @brief Зручний варіант `log_vprint` із варіадичними аргументами.
 * @param level Рівень.
 * @param fmt Форматний рядок `printf` українською.
 * @param ... Параметри форматування.
 */
void log_print (log_level_t level, const char *fmt, ...);

/** \brief Макрос для друку повідомлення рівня ERROR. */
#define LOGE(...) log_print (LOG_ERROR, __VA_ARGS__)
/** \brief Макрос для друку повідомлення рівня WARN. */
#define LOGW(...) log_print (LOG_WARN, __VA_ARGS__)
/** \brief Макрос для друку повідомлення рівня INFO. */
#define LOGI(...) log_print (LOG_INFO, __VA_ARGS__)
/** \brief Макрос для друку повідомлення рівня DEBUG. */
#define LOGD(...) log_print (LOG_DEBUG, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif
