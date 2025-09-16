/**
 * @file config.c
 * @brief Minimal JSON-backed persistent configuration implementation.
 */
#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef _WIN32
#define PATH_SEP '\\'
#else
#define PATH_SEP '/'
#endif

#define CONFIG_VERSION 1

#include "log.h"

typedef struct device_profile {
    const char *model;
    int orientation;
    double paper_w_mm;
    double paper_h_mm;
    double speed_mm_s;
    double accel_mm_s2;
} device_profile_t;

static const device_profile_t k_device_profiles[] = {
    { "minikit2", 1, 160.0, 101.0, 254.0, 200.0 },
    { "axidraw_v3", 1, 300.0, 218.0, 381.0, 250.0 },
};

static int strings_equal_ci (const char *a, const char *b) {
    if (!a || !b)
        return 0;
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a;
        unsigned char cb = (unsigned char)*b;
        if (tolower (ca) != tolower (cb))
            return 0;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static const device_profile_t *profile_for_model (const char *device_model) {
    if (device_model && *device_model) {
        for (size_t i = 0; i < sizeof (k_device_profiles) / sizeof (k_device_profiles[0]); ++i) {
            if (strings_equal_ci (device_model, k_device_profiles[i].model))
                return &k_device_profiles[i];
        }
    }
    for (size_t i = 0; i < sizeof (k_device_profiles) / sizeof (k_device_profiles[0]); ++i) {
        if (strings_equal_ci (CONFIG_DEFAULT_MODEL, k_device_profiles[i].model))
            return &k_device_profiles[i];
    }
    return &k_device_profiles[0];
}

/**
 * Перевірити, чи існує каталог за заданим шляхом.
 *
 * @param p Шлях до каталогу.
 * @return 1, якщо існує і це каталог; 0 інакше.
 */
static int dir_exists (const char *p) {
    struct stat st;
    if (stat (p, &st) == 0 && S_ISDIR (st.st_mode))
        return 1;
    return 0;
}

/**
 * Рекурсивно створити каталоги за шляхом (аналог `mkdir -p`).
 *
 * @param path Повний шлях до каталогу, який слід створити.
 * @return 0 у разі успіху; -1 при помилці.
 */
static int mkdir_p (const char *path) {
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
            if (!dir_exists (tmp)) {
                if (mkdir (tmp, 0700) != 0 && errno != EEXIST)
                    return -1;
            }
            *p = PATH_SEP;
        }
    }
    if (!dir_exists (tmp)) {
        if (mkdir (tmp, 0700) != 0 && errno != EEXIST)
            return -1;
    }
    return 0;
}

/**
 * Заповнити структуру конфігурації типовими (заводськими) значеннями.
 *
 * @param c Вказівник на конфігурацію для ініціалізації (безпечно передавати NULL — нічого не
 * робить).
 */
int config_factory_defaults (config_t *c, const char *device_model) {
    if (!c)
        return -1;
    const char *model = (device_model && *device_model) ? device_model : CONFIG_DEFAULT_MODEL;
    const device_profile_t *profile = profile_for_model (model);
    memset (c, 0, sizeof (*c));
    c->version = CONFIG_VERSION;
    c->orientation = 1;
    c->paper_w_mm = 160.0;
    c->paper_h_mm = 101.0;
    c->margin_top_mm = 10.0;
    c->margin_right_mm = 10.0;
    c->margin_bottom_mm = 10.0;
    c->margin_left_mm = 10.0;
    c->speed_mm_s = 254.0;
    c->accel_mm_s2 = 200.0;
    c->pen_up_pos = 60;
    c->pen_down_pos = 40;
    c->pen_up_speed = 150;
    c->pen_down_speed = 150;
    c->pen_up_delay_ms = 0;
    c->pen_down_delay_ms = 0;
    c->servo_timeout_s = 60;
    if (profile) {
        c->orientation = profile->orientation;
        c->paper_w_mm = profile->paper_w_mm;
        c->paper_h_mm = profile->paper_h_mm;
        c->speed_mm_s = profile->speed_mm_s;
        c->accel_mm_s2 = profile->accel_mm_s2;
    }
    return 0;
}

/**
 * Визначити шлях до файлу конфігурації користувача.
 *
 * Повертає шлях за XDG_CONFIG_HOME, або $HOME/.config/cplot/config.json, або ./config.json.
 *
 * @param buf    Буфер для запису шляху.
 * @param buflen Розмір буфера в байтах.
 * @return 0 у разі успіху; -1, якщо буфер надто малий або buf==NULL.
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
        LOGD ("шлях конфігурації з XDG_CONFIG_HOME: %s", buf);
        return 0;
    }
    if (home && home[0]) {
        int written = snprintf (
            buf, buflen, "%s%c.config%ccplot%cconfig.json", home, PATH_SEP, PATH_SEP, PATH_SEP);
        if (written < 0 || (size_t)written >= buflen) {
            LOGE ("шлях конфігурації надто довгий для буфера");
            return -1;
        }
        LOGD ("шлях конфігурації з $HOME: %s", buf);
        return 0;
    }
    int written = snprintf (buf, buflen, "./config.json");
    if (written < 0 || (size_t)written >= buflen) {
        LOGE ("шлях конфігурації надто довгий для буфера");
        return -1;
    }
    LOGW ("не вдалося визначити XDG/HOME; використано локальний шлях: %s", buf);
    return 0;
}

/**
 * Дуже простий розбірник JSON для наших ключів.
 *
 * Використовує пошук підрядків і перетворення чисел; призначений лише для файлу
 * конфігурації цього застосунку.
 *
 * @param s Рядок JSON.
 * @param c Конфігурація для заповнення значеннями (не NULL).
 * @return 0 у разі успіху; -1 при некоректних вхідних параметрах.
 */
static int parse_json (const char *s, config_t *c) {
    if (!s || !c)
        return -1;
    /* naive parsing: use strstr to find numbers */
    struct {
        const char *k;
        double *d;
        int *i;
    } map[] = {
        { "paper_w_mm", &c->paper_w_mm, NULL },
        { "paper_h_mm", &c->paper_h_mm, NULL },
        { "margin_top_mm", &c->margin_top_mm, NULL },
        { "margin_right_mm", &c->margin_right_mm, NULL },
        { "margin_bottom_mm", &c->margin_bottom_mm, NULL },
        { "margin_left_mm", &c->margin_left_mm, NULL },
        { "speed_mm_s", &c->speed_mm_s, NULL },
        { "accel_mm_s2", &c->accel_mm_s2, NULL },
        { "orientation", NULL, &c->orientation },
        { "pen_up_pos", NULL, &c->pen_up_pos },
        { "pen_down_pos", NULL, &c->pen_down_pos },
        { "pen_up_speed", NULL, &c->pen_up_speed },
        { "pen_down_speed", NULL, &c->pen_down_speed },
        { "pen_up_delay_ms", NULL, &c->pen_up_delay_ms },
        { "pen_down_delay_ms", NULL, &c->pen_down_delay_ms },
        { "servo_timeout_s", NULL, &c->servo_timeout_s },
        { "version", NULL, &c->version },
    };
    for (size_t i = 0; i < sizeof (map) / sizeof (map[0]); ++i) {
        const char *p = strstr (s, map[i].k);
        if (!p)
            continue;
        p = strchr (p, ':');
        if (!p)
            continue;
        p++;
        while (*p == ' ' || *p == '\t')
            p++;
        if (map[i].d) {
            map[i].d[0] = strtod (p, NULL);
        } else if (map[i].i) {
            map[i].i[0] = (int)strtol (p, NULL, 10);
        }
    }
    return 0;
}

/**
 * Записати конфігурацію у файл у форматі JSON.
 *
 * @param fp Відкритий файловий дескриптор для запису (у текстовому режимі).
 * @param c  Конфігурація для серіалізації (не NULL).
 * @return 0 у разі успіху; -1 при помилці в fprintf.
 */
static int write_json (FILE *fp, const config_t *c) {
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
        "  \"speed_mm_s\": %.3f,\n"
        "  \"accel_mm_s2\": %.3f,\n"
        "  \"pen_up_pos\": %d,\n"
        "  \"pen_down_pos\": %d,\n"
        "  \"pen_up_speed\": %d,\n"
        "  \"pen_down_speed\": %d,\n"
        "  \"pen_up_delay_ms\": %d,\n"
        "  \"pen_down_delay_ms\": %d,\n"
        "  \"servo_timeout_s\": %d\n"
        "}\n",
        c->version, c->orientation, c->paper_w_mm, c->paper_h_mm, c->margin_top_mm,
        c->margin_right_mm, c->margin_bottom_mm, c->margin_left_mm, c->speed_mm_s, c->accel_mm_s2,
        c->pen_up_pos, c->pen_down_pos, c->pen_up_speed, c->pen_down_speed, c->pen_up_delay_ms,
        c->pen_down_delay_ms, c->servo_timeout_s);
    return (n < 0) ? -1 : 0;
}

/**
 * Перевірити валідність значень конфігурації.
 *
 * @param c      Конфігурація для перевірки (не NULL).
 * @param err    Буфер для тексту помилки українською (може бути NULL).
 * @param errlen Розмір буфера помилки.
 * @return 0 — валідно; <0 — код помилки, а err (якщо надано) містить опис.
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
    if (c->orientation != 1 && c->orientation != 2) {
        if (err)
            snprintf (err, errlen, "Орієнтація має бути 1 (портрет) або 2 (альбом)");
        return -5;
    }
    if (c->speed_mm_s <= 0 || c->speed_mm_s > 2000) {
        if (err)
            snprintf (err, errlen, "Швидкість поза діапазоном");
        return -6;
    }
    if (c->accel_mm_s2 <= 0 || c->accel_mm_s2 > 50000) {
        if (err)
            snprintf (err, errlen, "Прискорення поза діапазоном");
        return -7;
    }
    if (c->pen_up_pos < 0 || c->pen_up_pos > 100 || c->pen_down_pos < 0 || c->pen_down_pos > 100) {
        if (err)
            snprintf (err, errlen, "Положення пера мають бути 0..100");
        return -8;
    }
    if (c->pen_up_speed < 1 || c->pen_up_speed > 1000 || c->pen_down_speed < 1
        || c->pen_down_speed > 1000) {
        if (err)
            snprintf (err, errlen, "Швидкості пера поза діапазоном");
        return -9;
    }
    if (c->pen_up_delay_ms < 0 || c->pen_up_delay_ms > 10000 || c->pen_down_delay_ms < 0
        || c->pen_down_delay_ms > 10000) {
        if (err)
            snprintf (err, errlen, "Затримки пера поза діапазоном");
        return -10;
    }
    if (c->servo_timeout_s < 0 || c->servo_timeout_s > 300) {
        if (err)
            snprintf (err, errlen, "Тайм-аут сервоприводу поза діапазоном (0..300)");
        return -11;
    }
    return 0;
}

/**
 * Завантажити конфігурацію з диску.
 *
 * За відсутності файлу повертає типові значення (factory defaults). Після
 * читання перевіряє валідність і за потреби повертає значення за замовчуванням.
 *
 * @param out Вказівник на конфігурацію для заповнення.
 * @return 0 у разі успіху; від'ємний код помилки інакше.
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
        /* no file: keep defaults */
        LOGI ("файл конфігурації не знайдено — використано типові значення");
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
    (void)parse_json (buf, out);
    free (buf);
    /* Validate */
    if (config_validate (out, NULL, 0) != 0) {
        /* If invalid, fall back to factory */
        config_factory_defaults (out, CONFIG_DEFAULT_MODEL);
        LOGW ("конфігурацію визнано невалідною — застосовано значення за замовчуванням");
    }
    return 0;
}

/**
 * Зберегти конфігурацію на диск атомарно (через тимчасовий файл і rename).
 *
 * @param cfg Конфігурація для збереження (не NULL).
 * @return 0 у разі успіху; від'ємний код помилки інакше.
 */
int config_save (const config_t *cfg) {
    if (!cfg)
        return -1;
    char path[512];
    if (config_get_path (path, sizeof (path)) != 0)
        return -2;

    /* Ensure parent directory exists */
    char dir[512];
    strncpy (dir, path, sizeof (dir) - 1);
    dir[sizeof (dir) - 1] = '\0';
    char *last_sep = strrchr (dir, PATH_SEP);
    if (last_sep) {
        *last_sep = '\0';
        if (mkdir_p (dir) != 0) {
            LOGE ("не вдалося створити каталог: %s", dir);
            return -7;
        }
    }

    /* Write to temp then rename */
    char tmp[576];
    snprintf (tmp, sizeof (tmp), "%s.tmp", path);
    FILE *fp = fopen (tmp, "wb");
    if (!fp) {
        LOGE ("не вдалося відкрити файл для запису: %s", tmp);
        return -3;
    }
    if (write_json (fp, cfg) != 0) {
        fclose (fp);
        remove (tmp);
        LOGE ("помилка під час запису конфігурації у JSON: %s", tmp);
        return -4;
    }
    if (fclose (fp) != 0) {
        remove (tmp);
        LOGE ("помилка під час закриття файлу: %s", tmp);
        return -5;
    }
    if (rename (tmp, path) != 0) {
        remove (tmp);
        LOGE ("не вдалося перейменувати %s у %s", tmp, path);
        return -6;
    }
    LOGI ("конфігурацію збережено: %s", path);
    return 0;
}

/**
 * Скинути конфігурацію до заводських налаштувань та зберегти її.
 *
 * @return 0 у разі успіху; від'ємний код помилки інакше.
 */
int config_reset (void) {
    config_t c;
    config_factory_defaults (&c, CONFIG_DEFAULT_MODEL);
    return config_save (&c);
}
