/**
 * @file log.c
 * @brief Реалізація рівнів журналювання та форматування повідомлень.
 * @ingroup log
 * @details
 * Виводить повідомлення у `stderr` з урахуванням порога рівня та кольорів,
 * що визначаються `isatty(stderr)` або змінною `CPLOT_LOG_NO_COLOR`.
 */
#include "log.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

/** \brief Глобальна конфігурація журналювання за замовчуванням. */
static log_config_t g_cfg = {
    .level = LOG_INFO,
    .use_colors = true,
};

/**
 * @copydoc log_get_config
 */
log_config_t *log_get_config (void) { return &g_cfg; }

/**
 * @copydoc log_set_level
 */
void log_set_level (log_level_t level) { g_cfg.level = level; }

/**
 * @copydoc log_set_use_colors
 */
void log_set_use_colors (bool use) {
    if (!use) {
        g_cfg.use_colors = false;
        return;
    }

    if (!isatty (fileno (stderr)))
        g_cfg.use_colors = false;
    else
        g_cfg.use_colors = true;
}

/** \brief Повертає локалізований рядок мітки рівня. */
static const char *level_label (log_level_t lv) {
    switch (lv) {
    case LOG_ERROR:
        return "помилка";
    case LOG_WARN:
        return "попередження";
    case LOG_INFO:
        return "інфо";
    case LOG_DEBUG:
        return "налагодження";
    default:
        return "";
    }
}

/** \brief Повертає ANSI‑колір для рівня з урахуванням `use_colors`. */
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
 * @copydoc log_vprint
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
 * @copydoc log_print
 */
void log_print (log_level_t level, const char *fmt, ...) {
    va_list ap;
    va_start (ap, fmt);
    log_vprint (level, fmt, ap);
    va_end (ap);
}

/**
 * @copydoc log_init_from_env
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
    }

    const char *nc = getenv ("CPLOT_LOG_NO_COLOR");
    if (nc && *nc) {
        if (strcmp (nc, "1") == 0 || strcasecmp (nc, "true") == 0 || strcasecmp (nc, "yes") == 0)
            g_cfg.use_colors = false;
        else
            log_set_use_colors (g_cfg.use_colors);
    } else {

        log_set_use_colors (g_cfg.use_colors);
    }

    return 0;
}
