/**
 * @file fontreg.c
 * @brief Parse hershey/index.json and resolve bundled font faces.
 */
#include "fontreg.h"

#include "jsr.h"
#include "log.h"
#include "trace.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Перевести рядок у нижній регістр in-place (ASCII).
 *
 * @param s Рядок, який буде змінено (може бути порожнім, не NULL).
 */
static void strlower (char *s) {
    for (; *s; ++s)
        *s = (char)tolower ((unsigned char)*s);
}

/**
 * Скопіювати рядок у буфер з гарантією завершення NUL.
 */
static void copy_string (char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0)
        return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy (dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

/**
 * Зберігає базовий каталог із ресурсами Hershey.
 *
 * Використовується для пошуку hershey/index.json та окремих SVG-файлів при
 * запуску з пакетів, де дані розміщено не поруч із бінарником.
 */
static char g_font_root[PATH_MAX];
static int g_font_root_initialized = 0;

/**
 * Встановити базовий каталог, відносно якого шукати hershey/.
 *
 * @param path Абсолютний шлях до каталогу з ресурсами або NULL для скидання.
 */
void fontreg_set_root (const char *path) {
    g_font_root_initialized = 1;
    if (!path || !*path) {
        g_font_root[0] = '\0';
        trace_write (LOG_INFO, "fontreg: скинуто базовий каталог" );
        return;
    }
    copy_string (g_font_root, sizeof (g_font_root), path);
    size_t len = strlen (g_font_root);
    while (len > 0 && g_font_root[len - 1] == '/') {
        g_font_root[len - 1] = '\0';
        --len;
    }
    trace_write (LOG_INFO, "fontreg: встановлено базовий каталог → %s", g_font_root);
}

/**
 * Отримати фактичний базовий каталог, враховуючи CPLOT_DATA_DIR.
 *
 * @return Вказівник на встановлений каталог або NULL, якщо використовується CWD.
 */
static const char *fontreg_effective_root (void) {
    if (!g_font_root_initialized) {
        const char *env = getenv ("CPLOT_DATA_DIR");
        fontreg_set_root (env && *env ? env : NULL);
    }
    return (g_font_root_initialized && g_font_root[0] != '\0') ? g_font_root : NULL;
}

/**
 * Прочитати список шрифтів із hershey/index.json.
 *
 * Читає весь файл як один JSON‑рядок (об’єкт верхнього рівня) ітерацією по
 * ключах верхнього рівня. Для читання значень використовуються утиліти jsr
 * (json_get_raw/json_get_string).
 *
 * @param faces  Вихід: масив елементів font_face_t (malloc), який потрібно звільнити через free().
 * @param count  Вихід: кількість елементів у масиві.
 * @return 0 у разі успіху; від'ємний код помилки інакше.
 */
int fontreg_list (font_face_t **faces, size_t *count) {
    if (!faces || !count)
        return -1;
    *faces = NULL;
    *count = 0;
    char index_path[PATH_MAX];
    FILE *fp = NULL;
    const char *root = fontreg_effective_root ();
    if (root) {
        int written = snprintf (index_path, sizeof (index_path), "%s/hershey/index.json", root);
        if (written > 0 && (size_t)written < sizeof (index_path))
            fp = fopen (index_path, "r");
        else
            LOGW ("шлях до hershey/index.json занадто довгий відносно бази: %s", root);
    }
    if (!fp)
        fp = fopen ("hershey/index.json", "r");
    if (!fp) {
        LOGE ("не знайдено hershey/index.json");
        trace_write (LOG_ERROR, "fontreg: не знайдено hershey/index.json (root=%s)",
            root ? root : "<cwd>");
        return -2;
    }
    /* Зчитати увесь файл у пам'ять */
    if (fseek (fp, 0, SEEK_END) != 0) {
        fclose (fp);
        return -3;
    }
    long sz = ftell (fp);
    if (sz < 0) {
        fclose (fp);
        return -3;
    }
    rewind (fp);
    char *json = (char *)malloc ((size_t)sz + 1);
    if (!json) {
        fclose (fp);
        return -3;
    }
    size_t rd = fread (json, 1, (size_t)sz, fp);
    fclose (fp);
    json[rd] = '\0';

    size_t cap = 16;
    font_face_t *arr = (font_face_t *)malloc (cap * sizeof (*arr));
    if (!arr) {
        free (json);
        trace_write (LOG_ERROR, "fontreg: нестача пам'яті під час читання index.json");
        return -3;
    }

    /* Ітерація по ключах верхнього рівня */
    const char *p = json_skip_ws (json);
    if (*p != '{') {
        free (arr);
        free (json);
        LOGE ("некоректний формат hershey/index.json (очікувався об’єкт)");
        trace_write (LOG_ERROR, "fontreg: некоректний формат index.json");
        return -4;
    }
    p++;
    while (1) {
        p = json_skip_ws (p);
        if (*p == '}') {
            break; /* кінець об’єкта */
        }
        if (*p != '"') {
            /* пропустити непридатний символ до наступної коми/закриття */
            while (*p && *p != ',' && *p != '}')
                p++;
            if (*p == ',') {
                p++;
                continue;
            }
            if (*p == '}')
                break;
        }
        /* Зняти ключ у лапках (без ескейпів для спрощення: у файлі вони відсутні) */
        p++;
        const char *kbeg = p;
        while (*p && *p != '"')
            p++;
        if (*p != '"')
            break;
        size_t klen = (size_t)(p - kbeg);
        char key[64];
        if (klen >= sizeof (key))
            klen = sizeof (key) - 1;
        memcpy (key, kbeg, klen);
        key[klen] = '\0';
        p++; /* після закритих лапок */
        p = json_skip_ws (p);
        if (*p != ':') {
            /* Спробувати продовжити */
            while (*p && *p != ',' && *p != '}')
                p++;
            if (*p == ',') {
                p++;
                continue;
            }
            if (*p == '}')
                break;
        }
        p++; /* на початок значення */
        p = json_skip_ws (p);

        /* Отримати сирий фрагмент значення за допомогою jsr (пошук від початку json) */
        const char *vptr = NULL;
        size_t vlen = 0;
        if (!json_get_raw (json, key, &vptr, &vlen)) {
            /* пропустити значення вручну */
            int depth = 0, in_str = 0;
            const char *q = p;
            while (*q) {
                if (!in_str) {
                    if (*q == '"')
                        in_str = 1;
                    else if (*q == '{' || *q == '[')
                        depth++;
                    else if (*q == '}' || *q == ']') {
                        if (depth == 0)
                            break;
                        depth--;
                    } else if (*q == ',' && depth == 0)
                        break;
                } else {
                    if (*q == '\\' && q[1]) {
                        q += 2;
                        continue;
                    }
                    if (*q == '"')
                        in_str = 0;
                }
                q++;
            }
            p = q;
            p = json_skip_ws (p);
            if (*p == ',')
                p++;
            continue;
        }
        /* Значення — об’єкт із полями file та name */
        char *file = json_get_string (vptr, "file", NULL);
        char *name = json_get_string (vptr, "name", NULL);
        if (file && name) {
            if (*count == cap) {
                cap *= 2;
                font_face_t *na = (font_face_t *)realloc (arr, cap * sizeof (*na));
                if (!na) {
                    free (file);
                    free (name);
                    free (arr);
                    free (json);
                    LOGE ("нестача пам’яті під час збільшення масиву шрифтів");
                    return -5;
                }
                arr = na;
            }
            memset (&arr[*count], 0, sizeof (arr[*count]));
            copy_string (arr[*count].id, sizeof (arr[*count].id), key);
            copy_string (arr[*count].name, sizeof (arr[*count].name), name);
            int written;
            if (root)
                written = snprintf (
                    arr[*count].path, sizeof (arr[*count].path), "%s/hershey/%s", root, file);
            else
                written
                    = snprintf (arr[*count].path, sizeof (arr[*count].path), "hershey/%s", file);
            if (written < 0 || (size_t)written >= sizeof (arr[*count].path)) {
                LOGW ("шлях до шрифту занадто довгий: hershey/%s", file);
            } else {
                (*count)++;
            }
        }
        free (file);
        free (name);

        /* Продовжити після значення */
        p = vptr + vlen;
        p = json_skip_ws (p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == '}')
            break;
    }

    *faces = arr;
    LOGD ("завантажено шрифтів: %zu", *count);
    trace_write (LOG_INFO, "fontreg: завантажено %zu шрифтів", *count);
    free (json);
    return 0;
}

/**
 * Знайти шрифт за ідентифікатором або частковим збігом імені (case-insensitive).
 *
 * Якщо @p query порожній, повертає шрифт за замовчуванням "hershey_sans_med".
 *
 * @param query Пошуковий рядок (може бути NULL або порожнім).
 * @param out   Вихід: знайдений шрифт.
 * @return 0 у разі успіху; -3 якщо не знайдено; інші від'ємні коди — помилки читання списку.
 */
int fontreg_resolve (const char *query, font_face_t *out) {
    if (!out)
        return -1;
    font_face_t *faces = NULL;
    size_t count = 0;
    if (fontreg_list (&faces, &count) != 0 || count == 0) {
        free (faces);
        LOGE ("не вдалося завантажити реєстр шрифтів");
        trace_write (LOG_ERROR, "fontreg: реєстр шрифтів недоступний");
        return -2;
    }
    /* Default to hershey_sans_med when query is NULL/empty */
    const char *fallback_id = "hershey_sans_med";
    int found = 0;
    if (query && *query) {
        char q[96];
        copy_string (q, sizeof (q), query);
        strlower (q);
        for (size_t i = 0; i < count; ++i) {
            char id[64];
            copy_string (id, sizeof (id), faces[i].id);
            strlower (id);
            char nm[96];
            copy_string (nm, sizeof (nm), faces[i].name);
            strlower (nm);
            if (strstr (id, q) || strstr (nm, q)) {
                *out = faces[i];
                found = 1;
                break;
            }
        }
    }
    if (!found) {
        for (size_t i = 0; i < count; ++i) {
            if (strcmp (faces[i].id, fallback_id) == 0) {
                *out = faces[i];
                found = 1;
                break;
            }
        }
    }
    free (faces);
    if (!found) {
        LOGW ("шрифт не знайдено: '%s'", (query && *query) ? query : "<типовий>");
        trace_write (
            LOG_WARN,
            "fontreg: шрифт не знайдено (%s)",
            (query && *query) ? query : "<типовий>");
        return -3;
    }
    LOGD ("вибрано шрифт: %s", out->id);
    trace_write (
        LOG_INFO,
        "fontreg: обрано шрифт %s (%s)",
        out->id,
        (query && *query) ? query : "<типовий>");
    return 0;
}
