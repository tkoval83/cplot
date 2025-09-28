/**
 * @file log.h
 * @brief Проста підсистема журналювання з рівнями та кольорами ANSI.
 */
#ifndef LOG_H
#define LOG_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

/* Колірні ANSI-коди винесено сюди, щоб уникнути попередження
 * "included header ... not used directly" для colors.h.
 * Якщо інші частини коду вже визначили ці макроси — не перевизначаємо. */
#ifndef RED
#define RED "\x1b[31m"
#endif
#ifndef BROWN
#define BROWN "\x1b[33m"
#endif
#ifndef BLUE
#define BLUE "\x1b[34m"
#endif
#ifndef GRAY
#define GRAY "\x1b[90m"
#endif
#ifndef NO_COLOR
#define NO_COLOR "\x1b[0m"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Рівень журналу. */
typedef enum {
    LOG_ERROR = 0,
    LOG_WARN = 1,
    LOG_INFO = 2,
    LOG_DEBUG = 3,
} log_level_t;

/**
 * Налаштування логера (глобальні).
 */
typedef struct log_config_s {
    log_level_t level; /* Мінімальний рівень для виводу */
    bool use_colors;   /* Використовувати ANSI-кольори */
} log_config_t;

/**
 * Отримати/встановити глобальну конфігурацію логера.
 */
log_config_t *log_get_config (void);
void log_set_level (log_level_t level);
void log_set_use_colors (bool use);

/**
 * Ініціалізувати логер із змінних середовища.
 *
 * Підтримувані змінні:
 *   CPLOT_LOG=debug|info|warn|error  (визначає мінімальний рівень)
 *   CPLOT_LOG_NO_COLOR=1             (вимикає кольоровий вивід)
 *
 * Не вважається фатальною помилкою, якщо змінні відсутні — зберігаються поточні налаштування.
 *
 * @return 0 при успішному застосуванні (або відсутності змін), ненульове значення при помилці.
 */
int log_init_from_env (void);

/**
 * Внутрішній друк повідомлення із зазначеним рівнем.
 * Повідомлення українською, формат printf.
 */
void log_vprint (log_level_t level, const char *fmt, va_list ap);
#if defined(__GNUC__) || defined(__clang__)
__attribute__ ((format (printf, 2, 3)))
#endif
void log_print (log_level_t level, const char *fmt, ...);

/* Зручні макроси для виклику */
#define LOGE(...) log_print (LOG_ERROR, __VA_ARGS__)
#define LOGW(...) log_print (LOG_WARN, __VA_ARGS__)
#define LOGI(...) log_print (LOG_INFO, __VA_ARGS__)
#define LOGD(...) log_print (LOG_DEBUG, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* LOG_H */
