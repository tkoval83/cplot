/**
 * @file log.h
 * @brief Проста підсистема журналювання з рівнями та кольорами ANSI.
 */
#ifndef LOG_H
#define LOG_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

#include "colors.h"

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
