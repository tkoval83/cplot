/**
 * @file trace.c
 * @brief Реалізація файлового трасування із циклічним обертанням логів.
 */
#include "trace.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <ctype.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define TRACE_PATH_SEP '\\'
#define trace_mkdir(path) _mkdir (path)
#define trace_unlink(path) _unlink (path)
#define trace_getcwd _getcwd
#else
#include <unistd.h>
#define TRACE_PATH_SEP '/'
#define trace_mkdir(path) mkdir ((path), 0755)
#define trace_unlink(path) unlink (path)
#define trace_getcwd getcwd
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define TRACE_DEFAULT_BYTES (1024u * 1024u) /**< 1 МіБ */
#define TRACE_DEFAULT_FILES 5u

static int trace_file_exists (const char *path) {
    if (!path || !*path)
        return 0;
#ifdef _WIN32
    return _access (path, 0) == 0;
#else
    return access (path, F_OK) == 0;
#endif
}

static void trace_trim_trailing_sep (char *path) {
    if (!path)
        return;
    size_t len = strlen (path);
    while (len > 0 && path[len - 1] == TRACE_PATH_SEP) {
#ifdef _WIN32
        if (len == 3 && isalpha ((unsigned char)path[0]) && path[1] == ':'
            && (path[2] == '\\' || path[2] == '/'))
            break;
        if (len == 2 && isalpha ((unsigned char)path[0]) && path[1] == ':')
            break;
#endif
        if (len == 1)
            break;
        path[--len] = '\0';
    }
}

static bool trace_path_is_root (const char *path) {
#ifdef _WIN32
    if (!path || !*path)
        return false;
    size_t len = strlen (path);
    if (len == 1 && (path[0] == '\\' || path[0] == '/'))
        return true;
    if (len == 2 && isalpha ((unsigned char)path[0]) && path[1] == ':')
        return true;
    if (len == 3 && isalpha ((unsigned char)path[0]) && path[1] == ':'
        && (path[2] == '\\' || path[2] == '/'))
        return true;
    return false;
#else
    return path && strcmp (path, "/") == 0;
#endif
}

static bool trace_up_one (char *path) {
    if (!path || !*path)
        return false;
    trace_trim_trailing_sep (path);
#ifdef _WIN32
    if (trace_path_is_root (path))
        return false;
#endif
    char *sep = strrchr (path, TRACE_PATH_SEP);
#ifdef _WIN32
    if (!sep) {
        size_t len = strlen (path);
        if (len == 2 && isalpha ((unsigned char)path[0]) && path[1] == ':')
            return false;
        return false;
    }
    if (sep == path + 2 && isalpha ((unsigned char)path[0]) && path[1] == ':') {
        sep[1] = '\0';
        return true;
    }
#else
    if (!sep)
        return false;
#endif
    if (sep == path) {
        sep[1] = '\0';
        return true;
    }
    *sep = '\0';
    return true;
}

static int find_project_root (char *buf, size_t buflen) {
    if (!buf || buflen == 0)
        return -1;
    char current[PATH_MAX];
    if (!trace_getcwd (current, sizeof (current)))
        return -1;

    while (true) {
        trace_trim_trailing_sep (current);
        char candidate[PATH_MAX];
        int written = snprintf (candidate, sizeof (candidate), "%s%cproject.conf", current, TRACE_PATH_SEP);
        if (written > 0 && (size_t)written < sizeof (candidate) && trace_file_exists (candidate)) {
            size_t len = strlen (current);
            if (len >= buflen)
                return -1;
            memcpy (buf, current, len + 1);
            return 0;
        }
        if (trace_path_is_root (current))
            break;
        if (!trace_up_one (current))
            break;
    }
    return -1;
}

typedef struct {
    bool enabled;
    FILE *fp;
    char path[PATH_MAX];
    size_t max_bytes;
    unsigned max_files;
    log_level_t level;
    size_t bytes_written;
} trace_state_t;

static trace_state_t g_trace = {
    .enabled = false,
    .fp = NULL,
    .path = { 0 },
    .max_bytes = TRACE_DEFAULT_BYTES,
    .max_files = TRACE_DEFAULT_FILES,
    .level = LOG_DEBUG,
    .bytes_written = 0,
};

static const char *level_label (log_level_t level) {
    switch (level) {
    case LOG_ERROR:
        return "помилка";
    case LOG_WARN:
        return "попередження";
    case LOG_INFO:
        return "інфо";
    case LOG_DEBUG:
        return "debug";
    default:
        return "?";
    }
}

static int trace_safe_vsnprintf (char *buf, size_t size, const char *fmt, va_list ap) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
    int rc = vsnprintf (buf, size, fmt, ap);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    return rc;
}

static int mkdir_recursive (const char *dir) {
    if (!dir || !*dir)
        return 0;
    size_t len = strlen (dir);
    if (len == 0 || len >= PATH_MAX)
        return -1;

    char tmp[PATH_MAX];
    memcpy (tmp, dir, len + 1);
    for (size_t i = 1; i < len; ++i) {
        if (tmp[i] == TRACE_PATH_SEP) {
            char save = tmp[i];
            tmp[i] = '\0';
            if (tmp[0]) {
                if (trace_mkdir (tmp) != 0 && errno != EEXIST) {
                    tmp[i] = save;
                    return -1;
                }
            }
            tmp[i] = save;
        }
    }
    if (trace_mkdir (tmp) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

static int ensure_parent_directory (const char *path) {
    if (!path)
        return -1;
    const char *sep = strrchr (path, TRACE_PATH_SEP);
    if (!sep)
        return 0;
    size_t len = (size_t)(sep - path);
    if (len == 0)
        return 0;
    if (len >= PATH_MAX)
        return -1;
    char buf[PATH_MAX];
    memcpy (buf, path, len);
    buf[len] = '\0';
    return mkdir_recursive (buf);
}

static int default_trace_path (char *buf, size_t buflen) {
    if (!buf || buflen == 0)
        return -1;
    char project_root[PATH_MAX];
    if (find_project_root (project_root, sizeof (project_root)) == 0) {
        int written = snprintf (
            buf, buflen, "%s%c%s%ctrace.log", project_root, TRACE_PATH_SEP, "log", TRACE_PATH_SEP);
        if (written < 0 || (size_t)written >= buflen)
            return -1;
        return 0;
    }
    const char *xdg = getenv ("XDG_STATE_HOME");
    const char *home = getenv ("HOME");
    if (xdg && xdg[0]) {
        int written = snprintf (buf, buflen, "%s%ccplot%ctrace.log", xdg, TRACE_PATH_SEP, TRACE_PATH_SEP);
        if (written < 0 || (size_t)written >= buflen)
            return -1;
        return 0;
    }
    if (home && home[0]) {
        int written = snprintf (
            buf,
            buflen,
            "%s%c.local%cstate%ccplot%ctrace.log",
            home,
            TRACE_PATH_SEP,
            TRACE_PATH_SEP,
            TRACE_PATH_SEP,
            TRACE_PATH_SEP);
        if (written < 0 || (size_t)written >= buflen)
            return -1;
        return 0;
    }
    int written = snprintf (buf, buflen, "./cplot-trace.log");
    if (written < 0 || (size_t)written >= buflen)
        return -1;
    return 0;
}

static void trace_close (void) {
    if (g_trace.fp) {
        fclose (g_trace.fp);
        g_trace.fp = NULL;
    }
}

static int trace_open (void) {
    if (!g_trace.enabled || g_trace.path[0] == '\0')
        return -1;
    if (g_trace.fp)
        return 0;
    if (ensure_parent_directory (g_trace.path) != 0)
        return -1;
    g_trace.fp = fopen (g_trace.path, "a");
    if (!g_trace.fp)
        return -1;
    if (fseek (g_trace.fp, 0, SEEK_END) == 0) {
        long pos = ftell (g_trace.fp);
        g_trace.bytes_written = (pos > 0) ? (size_t)pos : 0;
    } else {
        g_trace.bytes_written = 0;
    }
    return 0;
}

static void trace_rotate (void) {
    if (!g_trace.enabled || g_trace.max_files == 0)
        return;
    trace_close ();

    char oldest[PATH_MAX];
    if (snprintf (oldest, sizeof (oldest), "%s.%u", g_trace.path, g_trace.max_files) > 0)
        trace_unlink (oldest);

    for (unsigned i = g_trace.max_files; i > 0; --i) {
        char src[PATH_MAX];
        char dst[PATH_MAX];
        if (i == 1)
            snprintf (src, sizeof (src), "%s", g_trace.path);
        else
            snprintf (src, sizeof (src), "%s.%u", g_trace.path, i - 1);
        snprintf (dst, sizeof (dst), "%s.%u", g_trace.path, i);
        rename (src, dst);
    }
    g_trace.bytes_written = 0;
}

static void format_timestamp (char *buf, size_t buflen) {
    if (!buf || buflen == 0)
        return;
    time_t now = time (NULL);
#ifdef _WIN32
    struct tm lt = { 0 };
    localtime_s (&lt, &now);
#else
    struct tm lt = { 0 };
    localtime_r (&now, &lt);
#endif
    if (strftime (buf, buflen, "%Y-%m-%d %H:%M:%S", &lt) == 0)
        snprintf (buf, buflen, "0000-00-00 00:00:00");
}

static void trace_write_payload (const char *payload, size_t len) {
    if (!payload || len == 0)
        return;
    if (trace_open () != 0)
        return;
    fwrite (payload, 1, len, g_trace.fp);
    g_trace.bytes_written += len;
    fflush (g_trace.fp);
}

int trace_enable (const trace_options_t *options) {
    trace_state_t previous = g_trace;
    trace_close ();

    size_t max_bytes = TRACE_DEFAULT_BYTES;
    unsigned max_files = TRACE_DEFAULT_FILES;
    log_level_t level = LOG_DEBUG;
    char path_buf[PATH_MAX];

    if (options) {
        if (options->max_bytes > 0)
            max_bytes = options->max_bytes;
        if (options->max_files > 0)
            max_files = options->max_files;
        if (options->level >= LOG_ERROR && options->level <= LOG_DEBUG)
            level = options->level;
        if (options->path && options->path[0]) {
            size_t len = strlen (options->path);
            if (len >= sizeof (path_buf))
                return -1;
            memcpy (path_buf, options->path, len + 1);
        }
    }

    if (!options || !options->path || !options->path[0]) {
        if (default_trace_path (path_buf, sizeof (path_buf)) != 0)
            return -1;
    }

    memset (&g_trace, 0, sizeof (g_trace));
    g_trace.enabled = true;
    g_trace.max_bytes = max_bytes;
    g_trace.max_files = max_files;
    g_trace.level = level;
    strncpy (g_trace.path, path_buf, sizeof (g_trace.path) - 1);
    g_trace.path[sizeof (g_trace.path) - 1] = '\0';

    if (trace_open () != 0) {
        g_trace = previous;
        if (previous.enabled && previous.path[0])
            trace_open ();
        return -1;
    }

    return 0;
}

bool trace_is_enabled (void) { return g_trace.enabled; }

void trace_vwrite (log_level_t level, const char *fmt, va_list ap) {
    if (!fmt || !g_trace.enabled || level > g_trace.level)
        return;

    char ts[32];
    format_timestamp (ts, sizeof (ts));

    char msg_stack[256];
    va_list ap_copy;
    va_copy (ap_copy, ap);
    int needed = trace_safe_vsnprintf (msg_stack, sizeof (msg_stack), fmt, ap_copy);
    va_end (ap_copy);

    const char *msg_ptr = msg_stack;
    char *msg_heap = NULL;
    if (needed < 0)
        msg_ptr = "(помилка формування повідомлення)";
    else if ((size_t)needed >= sizeof (msg_stack)) {
        size_t alloc = (size_t)needed + 1;
        msg_heap = (char *)malloc (alloc);
        if (!msg_heap)
            msg_ptr = "(нестача пам'яті для повідомлення)";
        else {
            va_copy (ap_copy, ap);
            trace_safe_vsnprintf (msg_heap, alloc, fmt, ap_copy);
            va_end (ap_copy);
            msg_ptr = msg_heap;
        }
    }

    char line_stack[512];
    int line_needed = snprintf (line_stack, sizeof (line_stack), "%s [%s] %s\n", ts, level_label (level), msg_ptr);

    const char *line_ptr = line_stack;
    char *line_heap = NULL;
    size_t line_len = 0;
    if (line_needed < 0) {
        line_ptr = "0000-00-00 00:00:00 [помилка] (запис трасування недоступний)\n";
        line_len = strlen (line_ptr);
    } else if ((size_t)line_needed >= sizeof (line_stack)) {
        size_t alloc = (size_t)line_needed + 1;
        line_heap = (char *)malloc (alloc);
        if (line_heap) {
            snprintf (line_heap, alloc, "%s [%s] %s\n", ts, level_label (level), msg_ptr);
            line_ptr = line_heap;
            line_len = strlen (line_ptr);
        } else {
            line_ptr = line_stack;
            line_len = sizeof (line_stack) - 1;
        }
    } else {
        line_len = (size_t)line_needed;
    }

    if (g_trace.max_bytes > 0 && line_len > g_trace.max_bytes)
        line_len = g_trace.max_bytes;

    if (g_trace.max_bytes > 0 && g_trace.bytes_written + line_len > g_trace.max_bytes)
        trace_rotate ();

    trace_write_payload (line_ptr, line_len);

    free (line_heap);
    free (msg_heap);
}

void trace_write (log_level_t level, const char *fmt, ...) {
    va_list ap;
    va_start (ap, fmt);
    trace_vwrite (level, fmt, ap);
    va_end (ap);
}

void trace_disable (void) {
    trace_close ();
    memset (&g_trace, 0, sizeof (g_trace));
    g_trace.max_bytes = TRACE_DEFAULT_BYTES;
    g_trace.max_files = TRACE_DEFAULT_FILES;
    g_trace.level = LOG_DEBUG;
}
