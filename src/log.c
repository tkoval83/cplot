/**
 * @file log.c
 * @brief Реалізація простої підсистеми журналювання.
 *
 * У цьому модулі визначено глобальну конфігурацію логера та утиліти
 * для друку повідомлень різних рівнів до stderr. Повідомлення мають бути
 * українською, формат сумісний із printf.
 */
#include "log.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

/**
 * @brief Глобальна конфігурація логера (рівень і використання кольорів).
 */
static log_config_t g_cfg = {
    .level = LOG_INFO,
    .use_colors = true,
};

/**
 * Отримати вказівник на глобальну конфігурацію логера.
 *
 * @return Вказівник на структуру конфігурації (ніколи не NULL).
 */
log_config_t *log_get_config (void) { return &g_cfg; }

/**
 * Встановити мінімальний рівень журналювання.
 *
 * Повідомлення з рівнем, вищим за встановлений (чисельно більшим),
 * не будуть виводитися.
 *
 * @param level Новий рівень (LOG_ERROR/LOG_WARN/LOG_INFO/LOG_DEBUG).
 */
void log_set_level (log_level_t level) { g_cfg.level = level; }

/**
 * Увімкнути/вимкнути ANSI‑кольори у виводі логера.
 *
 * @param use true — використовувати кольори; false — без кольорів.
 */
void log_set_use_colors (bool use) {
    if (!use) {
        g_cfg.use_colors = false;
        return;
    }
    /* Автовимкнення кольорів, якщо stderr не є TTY */
    if (!isatty (fileno (stderr)))
        g_cfg.use_colors = false;
    else
        g_cfg.use_colors = true;
}

/**
 * Повернути текстову мітку для рівня журналу українською мовою.
 *
 * @param lv Рівень журналу.
 * @return Статичний рядок з міткою рівня.
 */
static const char *level_label (log_level_t lv) {
    switch (lv) {
    case LOG_ERROR:
        return "помилка"; /* error */
    case LOG_WARN:
        return "попередження"; /* warn */
    case LOG_INFO:
        return "інфо"; /* info */
    case LOG_DEBUG:
        return "налагодження";
    default:
        return "";
    }
}

/**
 * Повернути ANSI‑колір для заданого рівня журналу згідно конфігурації.
 *
 * @param lv Рівень журналу.
 * @return Секвенція кольору або порожній рядок, якщо кольори вимкнено.
 */
static const char *level_color (log_level_t lv) {
    if (!g_cfg.use_colors)
        return "";
    switch (lv) {
    case LOG_ERROR:
        return RED;
    case LOG_WARN:
        return BROWN;
    case LOG_INFO:
        return BLUE;
    case LOG_DEBUG:
        return GRAY;
    default:
        return "";
    }
}

/**
 * Надрукувати повідомлення до stderr, використовуючи список аргументів.
 *
 * Функція поважає глобальну конфігурацію рівня та кольорів. Якщо рівень
 * повідомлення вищий за поточний, воно ігнорується.
 *
 * @param level Рівень повідомлення.
 * @param fmt   Форматний рядок (українською), як у printf.
 * @param ap    Список аргументів для форматування.
 */
void log_vprint (log_level_t level, const char *fmt, va_list ap) {
    if (!fmt)
        return;

    if (level > g_cfg.level)
        return;
    const char *col = level_color (level);
    const char *lab = level_label (level);
    if (g_cfg.use_colors && col[0])
        fprintf (stderr, "%s[%s]%s ", col, lab, NO_COLOR);
    else
        fprintf (stderr, "[%s] ", lab);
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
    vfprintf (stderr, fmt, ap);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    fputc ('\n', stderr);
}

/**
 * Зручна обгортка над log_vprint із варіативними аргументами.
 *
 * @param level Рівень повідомлення.
 * @param fmt   Форматний рядок (українською), як у printf.
 * @param ...   Додаткові аргументи згідно fmt.
 */
void log_print (log_level_t level, const char *fmt, ...) {
    va_list ap;
    va_start (ap, fmt);
    log_vprint (level, fmt, ap);
    va_end (ap);
}

/**
 * Ініціалізація логера зі змінних середовища.
 *
 * CPLOT_LOG           = debug|info|warn|error (регістр ігнорується)
 * CPLOT_LOG_NO_COLOR  = 1|true|yes (вимикає кольори)
 *
 * @return 0 (поточна реалізація не повертає детальні коди помилок)
 */
int log_init_from_env (void) {
    const char *lvl = getenv ("CPLOT_LOG");
    if (lvl && *lvl) {
        if (strcasecmp (lvl, "debug") == 0)
            log_set_level (LOG_DEBUG);
        else if (strcasecmp (lvl, "info") == 0)
            log_set_level (LOG_INFO);
        else if (strcasecmp (lvl, "warn") == 0 || strcasecmp (lvl, "warning") == 0)
            log_set_level (LOG_WARN);
        else if (strcasecmp (lvl, "error") == 0)
            log_set_level (LOG_ERROR);
        /* Ігноруємо невідоме значення — залишаємо поточний рівень */
    }

    const char *nc = getenv ("CPLOT_LOG_NO_COLOR");
    if (nc && *nc) {
        if (strcmp (nc, "1") == 0 || strcasecmp (nc, "true") == 0 || strcasecmp (nc, "yes") == 0)
            g_cfg.use_colors = false;
        else
            log_set_use_colors (g_cfg.use_colors); /* переоцінка TTY */
    } else {
        /* Перевірити TTY ще раз для поточного налаштування */
        log_set_use_colors (g_cfg.use_colors);
    }

    return 0;
}
