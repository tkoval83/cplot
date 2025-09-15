/**
 * @file fontreg.c
 * @brief Parse hershey/index.json and resolve bundled font faces.
 */
#include "fontreg.h"

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
 * Прочитати список шрифтів із hershey/index.json.
 *
 * Дуже простий пострічковий розбір очікує об'єкти вигляду
 * "key": { "file": "...", "name": "..." } на кожному рядку.
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
    size_t cap = 16;
    font_face_t *arr = (font_face_t *)malloc (cap * sizeof (*arr));
    if (!arr) {
        fclose (fp);
        return -3;
    }
    char line[512];
    while (fgets (line, sizeof (line), fp)) {
        char *kq = strchr (line, '"');
        if (!kq)
            continue;
        char *kend = strchr (kq + 1, '"');
        if (!kend)
            continue;
        size_t klen = (size_t)(kend - (kq + 1));
        if (klen >= sizeof (arr[0].id))
            klen = sizeof (arr[0].id) - 1;
        char key[64];
        memcpy (key, kq + 1, klen);
        key[klen] = '\0';
        char *filep = strstr (line, "\"file\"");
        char *namep = strstr (line, "\"name\"");
        if (!filep || !namep)
            continue;
        char *fq1 = strchr (filep + 6, '"');
        if (!fq1)
            continue;
        char *fq2 = strchr (fq1 + 1, '"');
        if (!fq2)
            continue;
        size_t flen = (size_t)(fq2 - (fq1 + 1));
        if (flen >= sizeof (arr[0].path))
            flen = sizeof (arr[0].path) - 1;
        char path[128];
        memcpy (path, fq1 + 1, flen);
        path[flen] = '\0';
        char *nq1 = strchr (namep + 6, '"');
        if (!nq1)
            continue;
        char *nq2 = strchr (nq1 + 1, '"');
        if (!nq2)
            continue;
        size_t nlen = (size_t)(nq2 - (nq1 + 1));
        if (nlen >= sizeof (arr[0].name))
            nlen = sizeof (arr[0].name) - 1;
        char name[96];
        memcpy (name, nq1 + 1, nlen);
        name[nlen] = '\0';

        if (*count == cap) {
            cap *= 2;
            font_face_t *na = (font_face_t *)realloc (arr, cap * sizeof (*na));
            if (!na) {
                free (arr);
                fclose (fp);
                LOGE ("нестача пам’яті під час збільшення масиву шрифтів");
                return -4;
            }
            arr = na;
        }
        memset (&arr[*count], 0, sizeof (arr[*count]));
        strncpy (arr[*count].id, key, sizeof (arr[*count].id) - 1);
        strncpy (arr[*count].name, name, sizeof (arr[*count].name) - 1);
        snprintf (arr[*count].path, sizeof (arr[*count].path), "hershey/%s", path);
        (*count)++;
    }
    fclose (fp);
    *faces = arr;
    LOGD ("завантажено шрифтів: %zu", *count);
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
        strncpy (q, query, sizeof (q) - 1);
        q[sizeof (q) - 1] = '\0';
        strlower (q);
        for (size_t i = 0; i < count; ++i) {
            char id[64];
            strncpy (id, faces[i].id, sizeof (id) - 1);
            id[sizeof (id) - 1] = '\0';
            strlower (id);
            char nm[96];
            strncpy (nm, faces[i].name, sizeof (nm) - 1);
            nm[sizeof (nm) - 1] = '\0';
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
