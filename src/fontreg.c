/**
 * @file fontreg.c
 * @brief Parse hershey/index.json and resolve bundled font faces.
 */
#include "fontreg.h"

#include "jsr.h"
#include "log.h"
#include <ctype.h>
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
    FILE *fp = fopen ("hershey/index.json", "r");
    if (!fp) {
        LOGE ("не знайдено hershey/index.json");
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
        return -3;
    }

    /* Ітерація по ключах верхнього рівня */
    const char *p = json_skip_ws (json);
    if (*p != '{') {
        free (arr);
        free (json);
        LOGE ("некоректний формат hershey/index.json (очікувався об’єкт)");
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
            int written
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
        return -3;
    }
    LOGD ("вибрано шрифт: %s", out->id);
    return 0;
}
