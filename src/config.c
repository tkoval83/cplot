/**
 * @file config.c
 * @brief Реалізація читання/запису конфігурації та її валідації.
 * @ingroup config
 */
#include "config.h"

#include "axidraw.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/** Роздільник шляху у файловій системі. */
#define PATH_SEP '/'

/** Поточна версія формату конфігурації. */
#define CONFIG_VERSION 4

#include "log.h"

/**
 * @brief Перевіряє, чи існує директорія.
 * @param p Шлях до каталогу.
 * @return 1 — існує, 0 — ні.
 */
static int config_dir_exists (const char *p) {
    struct stat st;
    if (stat (p, &st) == 0 && S_ISDIR (st.st_mode))
        return 1;
    return 0;
}

/**
 * @brief Рекурсивно створює усі проміжні каталоги (аналог `mkdir -p`).
 * @param path Повний шлях до каталогу/файлу.
 * @return 0 — успіх, -1 — помилка створення.
 */
static int config_mkdir_p (const char *path) {
    if (!path || !path[0])
        return -1;
    char tmp[512];
    strncpy (tmp, path, sizeof (tmp) - 1);
    tmp[sizeof (tmp) - 1] = '\0';
    size_t len = strlen (tmp);
    if (len == 0)
        return -1;
    if (tmp[len - 1] == PATH_SEP)
        tmp[len - 1] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == PATH_SEP) {
            *p = '\0';
            if (!config_dir_exists (tmp)) {
                if (mkdir (tmp, 0700) != 0 && errno != EEXIST)
                    return -1;
            }
            *p = PATH_SEP;
        }
    }
    if (!config_dir_exists (tmp)) {
        if (mkdir (tmp, 0700) != 0 && errno != EEXIST)
            return -1;
    }
    return 0;
}

/**
 * @brief Записує рядок у JSON зі екрануванням.
 * @param fp Файл.
 * @param value Рядок (може бути NULL).
 */
static void config_json_write_string (FILE *fp, const char *value) {
    fputc ('"', fp);
    if (value) {
        for (const char *p = value; *p; ++p) {
            switch (*p) {
            case '"':
            case '\\':
                fputc ('\\', fp);
                fputc (*p, fp);
                break;
            case '\n':
                fputs ("\\n", fp);
                break;
            case '\t':
                fputs ("\\t", fp);
                break;
            default:
                fputc (*p, fp);
                break;
            }
        }
    }
    fputc ('"', fp);
}

/**
 * @brief Забезпечує існування батьківського каталогу для шляху.
 * @param path Повний шлях до файлу.
 * @return 0 — ок, -1 — помилка.
 */
static int config_ensure_parent_dir (const char *path) {
    if (!path || !*path)
        return -1;
    char dir[512];
    strncpy (dir, path, sizeof (dir) - 1);
    dir[sizeof (dir) - 1] = '\0';
    char *last_sep = strrchr (dir, PATH_SEP);
    if (!last_sep)
        return 0;
    *last_sep = '\0';
    if (dir[0] == '\0')
        return 0;
    if (config_mkdir_p (dir) != 0) {
        LOGE ("не вдалося створити каталог: %s", dir);
        log_print (LOG_ERROR, "конфігурація: не вдалося створити каталог %s", dir);
        return -1;
    }
    return 0;
}

/**
 * @brief Заповнює конфіг значеннями за замовчуванням та застосовує профіль моделі.
 * @param c [out] Конфігурація.
 * @param device_model Модель (NULL — типова).
 * @return 0 — успіх, -1 — помилка.
 */
int config_factory_defaults (config_t *c, const char *device_model) {
    if (!c)
        return -1;
    const char *requested_model
        = (device_model && *device_model) ? device_model : CONFIG_DEFAULT_MODEL;
    memset (c, 0, sizeof (*c));
    c->version = CONFIG_VERSION;
    c->orientation = ORIENT_PORTRAIT;
    c->margin_top_mm = 10.0;
    c->margin_right_mm = 10.0;
    c->margin_bottom_mm = 10.0;
    c->margin_left_mm = 10.0;
    c->font_size_pt = 14.0;
    strncpy (c->font_family, CONFIG_DEFAULT_FONT_FAMILY, sizeof (c->font_family) - 1);
    c->font_family[sizeof (c->font_family) - 1] = '\0';
    c->pen_up_pos = 60;
    c->pen_down_pos = 40;
    c->pen_up_speed = 150;
    c->pen_down_speed = 150;
    c->pen_up_delay_ms = 0;
    c->pen_down_delay_ms = 0;
    c->servo_timeout_s = 60;
    c->default_device[0] = '\0';
    const axidraw_device_profile_t *profile = axidraw_device_profile_for_model (requested_model);
    axidraw_device_profile_apply (c, profile);
    log_print (
        LOG_DEBUG, "конфігурація: застосовано типовий профіль для моделі=%s (кегль=%.1f)",
        requested_model, c->font_size_pt);
    return 0;
}

/**
 * @brief Обчислює XDG-шлях до файлу конфігурації.
 * @param buf [out] Буфер для шляху.
 * @param buflen Довжина буфера.
 * @return 0 — успіх, -1 — помилка/замалий буфер.
 */
int config_get_path (char *buf, size_t buflen) {
    if (!buf || buflen < 8)
        return -1;
    const char *xdg = getenv ("XDG_CONFIG_HOME");
    const char *home = getenv ("HOME");
    if (xdg && xdg[0]) {
        int written = snprintf (buf, buflen, "%s%ccplot%cconfig.json", xdg, PATH_SEP, PATH_SEP);
        if (written < 0 || (size_t)written >= buflen) {
            LOGE ("шлях конфігурації надто довгий для буфера");
            return -1;
        }
        LOGD ("шлях конфігурації зі змінної середовища: %s", buf);
        log_print (LOG_DEBUG, "конфігурація: шлях за змінною середовища → %s", buf);
        return 0;
    }
    if (home && home[0]) {
        int written = snprintf (
            buf, buflen, "%s%c.config%ccplot%cconfig.json", home, PATH_SEP, PATH_SEP, PATH_SEP);
        if (written < 0 || (size_t)written >= buflen) {
            LOGE ("шлях конфігурації надто довгий для буфера");
            return -1;
        }
        LOGD ("шлях конфігурації з домашнього каталогу: %s", buf);
        log_print (LOG_DEBUG, "конфігурація: шлях з домашнього каталогу → %s", buf);
        return 0;
    }
    int written = snprintf (buf, buflen, "./config.json");
    if (written < 0 || (size_t)written >= buflen) {
        LOGE ("шлях конфігурації надто довгий для буфера");
        return -1;
    }
    LOGW ("не вдалося визначити змінні середовища; використано локальний шлях: %s", buf);
    log_print (LOG_WARN, "конфігурація: запасний шлях → %s", buf);
    return 0;
}

/**
 * @brief Дуже простий парсер JSON (плоскі поля) для конфігурації.
 * @param s Вміст JSON.
 * @param c [out] Конфігурація.
 * @return 0 — успіх, -1 — помилка.
 */
static int config_parse_json (const char *s, config_t *c) {
    if (!s || !c)
        return -1;
    typedef enum { FIELD_DOUBLE, FIELD_INT, FIELD_ENUM, FIELD_STRING } field_kind_t;

    struct field_spec {
        const char *key;
        field_kind_t kind;
        void *ptr;
        size_t str_capacity;
    } specs[] = {
        { "paper_w_mm", FIELD_DOUBLE, &c->paper_w_mm, 0 },
        { "paper_h_mm", FIELD_DOUBLE, &c->paper_h_mm, 0 },
        { "margin_top_mm", FIELD_DOUBLE, &c->margin_top_mm, 0 },
        { "margin_right_mm", FIELD_DOUBLE, &c->margin_right_mm, 0 },
        { "margin_bottom_mm", FIELD_DOUBLE, &c->margin_bottom_mm, 0 },
        { "margin_left_mm", FIELD_DOUBLE, &c->margin_left_mm, 0 },
        { "speed_mm_s", FIELD_DOUBLE, &c->speed_mm_s, 0 },
        { "accel_mm_s2", FIELD_DOUBLE, &c->accel_mm_s2, 0 },
        { "font_size_pt", FIELD_DOUBLE, &c->font_size_pt, 0 },
        { "orientation", FIELD_ENUM, &c->orientation, 0 },
        { "pen_up_pos", FIELD_INT, &c->pen_up_pos, 0 },
        { "pen_down_pos", FIELD_INT, &c->pen_down_pos, 0 },
        { "pen_up_speed", FIELD_INT, &c->pen_up_speed, 0 },
        { "pen_down_speed", FIELD_INT, &c->pen_down_speed, 0 },
        { "pen_up_delay_ms", FIELD_INT, &c->pen_up_delay_ms, 0 },
        { "pen_down_delay_ms", FIELD_INT, &c->pen_down_delay_ms, 0 },
        { "servo_timeout_s", FIELD_INT, &c->servo_timeout_s, 0 },
        { "version", FIELD_INT, &c->version, 0 },
        { "font_family", FIELD_STRING, c->font_family, sizeof (c->font_family) },
        { "default_device", FIELD_STRING, c->default_device, sizeof (c->default_device) },
    };

    const char *p = s;
    while (*p) {
        while (*p
               && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',' || *p == '{'
                   || *p == '}'))
            ++p;
        if (*p != '"') {
            if (*p)
                ++p;
            continue;
        }
        ++p;
        const char *key_start = p;
        while (*p && *p != '"') {
            if (*p == '\\' && p[1]) {
                p += 2;
                continue;
            }
            ++p;
        }
        if (*p != '"')
            break;
        size_t key_len = (size_t)(p - key_start);
        char key_buf[32];
        if (key_len >= sizeof (key_buf)) {
            ++p;
            continue;
        }
        memcpy (key_buf, key_start, key_len);
        key_buf[key_len] = '\0';
        ++p;
        while (*p == ' ' || *p == '\t')
            ++p;
        if (*p != ':')
            continue;
        ++p;
        while (*p == ' ' || *p == '\t')
            ++p;

        struct field_spec *spec = NULL;
        for (size_t i = 0; i < sizeof (specs) / sizeof (specs[0]); ++i) {
            if (strcmp (key_buf, specs[i].key) == 0) {
                spec = &specs[i];
                break;
            }
        }
        if (!spec)
            continue;

        if (spec->kind == FIELD_STRING) {
            if (*p != '"')
                continue;
            ++p;
            char *dest = (char *)spec->ptr;
            size_t cap = spec->str_capacity;
            size_t len = 0;
            while (*p && *p != '"') {
                char ch = *p++;
                if (ch == '\\' && *p) {
                    char esc = *p++;
                    switch (esc) {
                    case 'n':
                        ch = '\n';
                        break;
                    case 't':
                        ch = '\t';
                        break;
                    case '\\':
                        ch = '\\';
                        break;
                    case '"':
                        ch = '"';
                        break;
                    default:
                        ch = esc;
                        break;
                    }
                }
                if (len + 1 < cap)
                    dest[len++] = ch;
            }
            if (cap > 0)
                dest[len < cap ? len : cap - 1] = '\0';
            if (*p == '"')
                ++p;
        } else {
            char *endptr = NULL;
            if (spec->kind == FIELD_DOUBLE) {
                double val = strtod (p, &endptr);
                if (endptr && endptr != p)
                    *(double *)spec->ptr = val;
            } else {
                long val = strtol (p, &endptr, 10);
                if (endptr && endptr != p) {
                    if (spec->kind == FIELD_INT)
                        *(int *)spec->ptr = (int)val;
                    else
                        *(orientation_t *)spec->ptr = (orientation_t)val;
                }
            }
            if (endptr)
                p = endptr;
        }
    }
    return 0;
}

/**
 * @brief Записує конфігурацію у JSON-формат у файл.
 * @param fp Відкритий файл для запису.
 * @param c Конфігурація.
 * @return 0 — успіх, -1 — помилка IO.
 */
static int config_write_json (FILE *fp, const config_t *c) {
    int n = fprintf (
        fp,
        "{\n"
        "  \"version\": %d,\n"
        "  \"orientation\": %d,\n"
        "  \"paper_w_mm\": %.3f,\n"
        "  \"paper_h_mm\": %.3f,\n"
        "  \"margin_top_mm\": %.3f,\n"
        "  \"margin_right_mm\": %.3f,\n"
        "  \"margin_bottom_mm\": %.3f,\n"
        "  \"margin_left_mm\": %.3f,\n"
        "  \"font_size_pt\": %.2f,\n"
        "  \"speed_mm_s\": %.3f,\n"
        "  \"accel_mm_s2\": %.3f,\n"
        "  \"pen_up_pos\": %d,\n"
        "  \"pen_down_pos\": %d,\n"
        "  \"pen_up_speed\": %d,\n"
        "  \"pen_down_speed\": %d,\n"
        "  \"pen_up_delay_ms\": %d,\n"
        "  \"pen_down_delay_ms\": %d,\n"
        "  \"servo_timeout_s\": %d,\n",
        c->version, c->orientation, c->paper_w_mm, c->paper_h_mm, c->margin_top_mm,
        c->margin_right_mm, c->margin_bottom_mm, c->margin_left_mm, c->font_size_pt, c->speed_mm_s,
        c->accel_mm_s2, c->pen_up_pos, c->pen_down_pos, c->pen_up_speed, c->pen_down_speed,
        c->pen_up_delay_ms, c->pen_down_delay_ms, c->servo_timeout_s);
    if (n < 0)
        return -1;
    if (fprintf (fp, "  \"font_family\": ") < 0)
        return -1;
    config_json_write_string (fp, c->font_family);
    if (fprintf (fp, ",\n  \"default_device\": ") < 0)
        return -1;
    config_json_write_string (fp, c->default_device);
    if (fprintf (fp, "\n}\n") < 0)
        return -1;
    return 0;
}

/**
 * @brief Перевіряє коректність значень конфігурації та межі.
 * @param c Конфігурація.
 * @param err [out] Повідомлення (може бути NULL).
 * @param errlen Довжина буфера повідомлення.
 * @return 0 — валідно, відʼємний код — помилка.
 */
int config_validate (const config_t *c, char *err, size_t errlen) {
    if (!c)
        return -1;
    if (c->paper_w_mm <= 0 || c->paper_h_mm <= 0) {
        if (err)
            snprintf (err, errlen, "Розмір паперу має бути > 0");
        return -2;
    }
    if (c->margin_top_mm < 0 || c->margin_right_mm < 0 || c->margin_bottom_mm < 0
        || c->margin_left_mm < 0) {
        if (err)
            snprintf (err, errlen, "Поля не можуть бути від’ємними");
        return -3;
    }
    if (c->margin_top_mm + c->margin_bottom_mm >= c->paper_h_mm
        || c->margin_left_mm + c->margin_right_mm >= c->paper_w_mm) {
        if (err)
            snprintf (err, errlen, "Поля перевищують доступну область друку");
        return -4;
    }
    if (c->font_size_pt <= 0.0 || c->font_size_pt > 200.0) {
        if (err)
            snprintf (err, errlen, "Кегль має бути в межах 0 < size ≤ 200 pt");
        return -5;
    }
    if (c->orientation != ORIENT_PORTRAIT && c->orientation != ORIENT_LANDSCAPE) {
        if (err)
            snprintf (err, errlen, "Орієнтація має бути 1 (портрет) або 2 (альбом)");
        return -6;
    }
    if (c->speed_mm_s <= 0 || c->speed_mm_s > 2000) {
        if (err)
            snprintf (err, errlen, "Швидкість поза діапазоном");
        return -7;
    }
    if (c->accel_mm_s2 <= 0 || c->accel_mm_s2 > 50000) {
        if (err)
            snprintf (err, errlen, "Прискорення поза діапазоном");
        return -8;
    }
    if (c->pen_up_pos < 0 || c->pen_up_pos > 100 || c->pen_down_pos < 0 || c->pen_down_pos > 100) {
        if (err)
            snprintf (err, errlen, "Положення пера мають бути 0..100");
        return -9;
    }
    if (c->pen_up_speed < 1 || c->pen_up_speed > 1000 || c->pen_down_speed < 1
        || c->pen_down_speed > 1000) {
        if (err)
            snprintf (err, errlen, "Швидкості пера поза діапазоном");
        return -10;
    }
    if (c->pen_up_delay_ms < 0 || c->pen_up_delay_ms > 10000 || c->pen_down_delay_ms < 0
        || c->pen_down_delay_ms > 10000) {
        if (err)
            snprintf (err, errlen, "Затримки пера поза діапазоном");
        return -11;
    }
    if (c->servo_timeout_s < 0 || c->servo_timeout_s > 300) {
        if (err)
            snprintf (err, errlen, "Тайм-аут сервоприводу поза діапазоном (0..300)");
        return -12;
    }
    return 0;
}

/**
 * @brief Завантажує конфігурацію з диска або застосовує типові значення.
 * @param out [out] Конфігурація.
 * @return 0 — успіх, відʼємний код — помилка IO/формату.
 */
int config_load (config_t *out) {
    if (!out)
        return -1;
    config_factory_defaults (out, CONFIG_DEFAULT_MODEL);
    char path[512];
    if (config_get_path (path, sizeof (path)) != 0)
        return -2;
    FILE *fp = fopen (path, "rb");
    if (!fp) {

        LOGI ("файл конфігурації не знайдено — використано типові значення");
        log_print (LOG_INFO, "конфігурація: файл %s відсутній — застосовано типові значення", path);
        return 0;
    }
    fseek (fp, 0, SEEK_END);
    long n = ftell (fp);
    if (n < 0) {
        fclose (fp);
        LOGE ("не вдалося отримати розмір файлу конфігурації");
        return -3;
    }
    rewind (fp);
    char *buf = (char *)malloc ((size_t)n + 1);
    if (!buf) {
        fclose (fp);
        LOGE ("нестача пам’яті для читання конфігурації (%ld байт)", n);
        return -4;
    }
    size_t rd = fread (buf, 1, (size_t)n, fp);
    buf[rd] = '\0';
    fclose (fp);
    (void)config_parse_json (buf, out);
    free (buf);
    out->version = CONFIG_VERSION;

    if (config_validate (out, NULL, 0) != 0) {

        config_factory_defaults (out, CONFIG_DEFAULT_MODEL);
        LOGW ("конфігурацію визнано невалідною — застосовано значення за замовчуванням");
        log_print (LOG_WARN, "конфігурація: файл %s містив некоректні дані", path);
    } else {
        log_print (LOG_INFO, "конфігурація: конфігурацію завантажено з %s", path);
    }
    return 0;
}

/**
 * @brief Зберігає конфігурацію як JSON у XDG-шляху.
 * @param cfg Конфігурація.
 * @return 0 — успіх, відʼємний код — помилка IO.
 */
int config_save (const config_t *cfg) {
    if (!cfg)
        return -1;
    char path[512];
    if (config_get_path (path, sizeof (path)) != 0)
        return -2;

    if (config_ensure_parent_dir (path) != 0)
        return -7;

    char tmp[576];
    snprintf (tmp, sizeof (tmp), "%s.tmp", path);
    FILE *fp = fopen (tmp, "wb");
    if (!fp) {
        LOGE ("не вдалося відкрити файл для запису: %s", tmp);
        log_print (LOG_ERROR, "конфігурація: не вдалося відкрити %s для запису", tmp);
        return -3;
    }
    if (config_write_json (fp, cfg) != 0) {
        fclose (fp);
        remove (tmp);
        LOGE ("помилка під час запису конфігурації у JSON: %s", tmp);
        log_print (LOG_ERROR, "конфігурація: помилка запису у %s", tmp);
        return -4;
    }
    if (fclose (fp) != 0) {
        remove (tmp);
        LOGE ("помилка під час закриття файлу: %s", tmp);
        log_print (LOG_ERROR, "конфігурація: не вдалося закрити %s", tmp);
        return -5;
    }
    if (rename (tmp, path) != 0) {
        remove (tmp);
        LOGE ("не вдалося перейменувати %s у %s", tmp, path);
        log_print (LOG_ERROR, "конфігурація: не вдалося перейменувати %s у %s", tmp, path);
        return -6;
    }
    LOGI ("конфігурацію збережено: %s", path);
    log_print (LOG_INFO, "конфігурація: конфігурацію збережено до %s", path);
    return 0;
}

/**
 * @brief Скидає конфігурацію до типових значень і зберігає на диск.
 * @return 0 — успіх, відʼємний код — помилка.
 */
int config_reset (void) {
    config_t c;
    config_factory_defaults (&c, CONFIG_DEFAULT_MODEL);
    log_print (LOG_INFO, "конфігурація: повернення до заводських налаштувань");
    return config_save (&c);
}
