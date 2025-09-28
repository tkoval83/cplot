/**
 * @file fontreg.c
 * @brief Parse hershey/index.json and resolve bundled font faces.
 */
#include "fontreg.h"

#include "font.h"
#include "jsr.h"
#include "log.h"
#include "str.h"
#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FONT_STYLE_REGULAR 0x01
#define FONT_STYLE_ITALIC 0x02
#define FONT_STYLE_BOLD 0x04

/**
 * @brief Опис конкретного варіанта шрифту (гарнітура, стиль, покриття кодових точок).
 */
typedef struct {
    font_face_t face;
    int style_flags;
    uint32_t *codepoints;
    size_t codepoint_count;
} font_variant_info_t;

/**
 * @brief Згруповані варіанти всередині однієї родини шрифтів.
 */
typedef struct {
    char key[64];
    char display[96];
    int capability_mask;
    font_variant_info_t *variants;
    size_t variant_count;
    size_t variant_cap;
} font_family_info_t;

static font_family_info_t *g_families = NULL;
static size_t g_family_count = 0;
static size_t g_family_cap = 0;
static int g_catalog_ready = 0;

/**
 * @brief Очистити кешований каталог сімейств/варіантів.
 */
static void fontreg_catalog_clear (void) {
    for (size_t i = 0; i < g_family_count; ++i) {
        font_family_info_t *family = &g_families[i];
        for (size_t j = 0; j < family->variant_count; ++j)
            free (family->variants[j].codepoints);
        free (family->variants);
    }
    free (g_families);
    g_families = NULL;
    g_family_count = 0;
    g_family_cap = 0;
    g_catalog_ready = 0;
}

static int cmp_uint32 (const void *a, const void *b) {
    uint32_t ua = *(const uint32_t *)a;
    uint32_t ub = *(const uint32_t *)b;
    if (ua < ub)
        return -1;
    if (ua > ub)
        return 1;
    return 0;
}

/**
 * @brief Вилучити дублікати з відсортованого масиву кодових точок.
 */
static void dedupe_codepoints (uint32_t *codes, size_t *count) {
    if (!codes || !count || *count == 0)
        return;
    size_t unique = 0;
    for (size_t i = 0; i < *count; ++i) {
        if (i == 0 || codes[i] != codes[i - 1])
            codes[unique++] = codes[i];
    }
    *count = unique;
}

/**
 * @brief Перевірити, чи слово відповідає маркеру стилю (italic/bold тощо).
 */
static int is_style_token (const char *token) {
    static const char *k_tokens[]
        = { "italic", "ital",  "oblique", "bold",   "regular",   "roman",    "medium", "light",
            "heavy",  "plain", "outline", "shadow", "condensed", "extended", "narrow", "book" };
    for (size_t i = 0; i < sizeof (k_tokens) / sizeof (k_tokens[0]); ++i) {
        if (strcmp (token, k_tokens[i]) == 0)
            return 1;
    }
    return 0;
}

/**
 * @brief Побудувати читабельну назву сімейства, відкидаючи маркери стилю.
 */
static void build_family_display (const char *name, char *out, size_t out_len) {
    if (!out || out_len == 0)
        return;
    out[0] = '\0';
    if (!name || !*name)
        return;
    char original[128];
    string_copy (original, sizeof (original), name);
    char lower[128];
    string_copy (lower, sizeof (lower), name);
    string_to_lower_ascii (lower);
    char *ctx = NULL;
    char *token = strtok_r (lower, " _-\t\n\r", &ctx);
    size_t written = 0;
    while (token) {
        if (!is_style_token (token)) {
            size_t offset = (size_t)(token - lower);
            size_t len = strlen (token);
            if (written > 0 && written + 1 < out_len)
                out[written++] = ' ';
            for (size_t i = 0; i < len && written + 1 < out_len; ++i)
                out[written++] = original[offset + i];
            out[written] = '\0';
        }
        token = strtok_r (NULL, " _-\t\n\r", &ctx);
    }
    if (written == 0)
        string_copy (out, out_len, name);
}

/**
 * @brief Визначити стильові прапори (regular/bold/italic) з опису шрифту.
 */
static int style_flags_from_face (const font_face_t *face) {
    if (!face)
        return FONT_STYLE_REGULAR;
    char lowered_name[128];
    string_copy (lowered_name, sizeof (lowered_name), face->name);
    string_to_lower_ascii (lowered_name);
    char lowered_id[128];
    string_copy (lowered_id, sizeof (lowered_id), face->id);
    string_to_lower_ascii (lowered_id);
    int flags = 0;
    if (strstr (lowered_name, "italic") || strstr (lowered_name, "oblique")
        || strstr (lowered_id, "italic") || strstr (lowered_id, "oblique"))
        flags |= FONT_STYLE_ITALIC;
    if (strstr (lowered_name, "bold") || strstr (lowered_name, "heavy")
        || strstr (lowered_id, "bold") || strstr (lowered_id, "heavy"))
        flags |= FONT_STYLE_BOLD;
    if (strstr (lowered_name, "regular") || strstr (lowered_name, "roman")
        || strstr (lowered_name, "book") || strstr (lowered_id, "regular")
        || strstr (lowered_id, "roman") || strstr (lowered_id, "book"))
        flags |= FONT_STYLE_REGULAR;
    if (flags == 0)
        flags = FONT_STYLE_REGULAR;
    return flags;
}

/**
 * @brief Прибрати суфікс зі строкового ідентифікатора, якщо він присутній.
 */
static void strip_suffix (char *str, const char *suffix) {
    size_t len = strlen (str);
    size_t slen = strlen (suffix);
    if (len >= slen && strcmp (str + len - slen, suffix) == 0)
        str[len - slen] = '\0';
}

/**
 * @brief Вивести нормалізований ключ сімейства та читабельну назву.
 *
 * Нормалізує `face->id`, прибираючи суфікси `_bold`, `_italic` тощо, і будує display-рядок.
 */
static void derive_family_key (
    const font_face_t *face, char *out_key, size_t key_len, char *out_display, size_t display_len) {
    if (out_key && key_len > 0) {
        char tmp[64];
        string_copy (tmp, sizeof (tmp), face->id);
        string_to_lower_ascii (tmp);
        strip_suffix (tmp, "_bold_italic");
        strip_suffix (tmp, "_italic");
        strip_suffix (tmp, "_bold");
        /*
         * Normalize common weight/style suffixes to derive a stable family key.
         * Some bundled fonts use a shortened "_med" suffix instead of
         * "_medium" (e.g., hershey_serif_med). Handle both forms to ensure
         * variants like Regular/Italic (Med) and Bold/Italic group under the
         * same family key ("hershey_serif").
         */
        strip_suffix (tmp, "_medium");
        strip_suffix (tmp, "_med");
        strip_suffix (tmp, "_regular");
        strip_suffix (tmp, "_light");
        strip_suffix (tmp, "_heavy");
        size_t len = strlen (tmp);
        while (len > 0 && tmp[len - 1] == '_') {
            tmp[len - 1] = '\0';
            --len;
        }
        if (len == 0)
            string_copy (tmp, sizeof (tmp), face->id);
        string_copy (out_key, key_len, tmp);
    }
    if (out_display && display_len > 0)
        build_family_display (face->name, out_display, display_len);
}

/**
 * @brief Знайти сімейство у кеші за нормалізованим ключем.
 */
static font_family_info_t *find_family_by_key (const char *key) {
    if (!key)
        return NULL;
    for (size_t i = 0; i < g_family_count; ++i) {
        if (strcmp (g_families[i].key, key) == 0)
            return &g_families[i];
    }
    return NULL;
}

/**
 * @brief Повернути наявне сімейство або створити новий запис.
 */
static font_family_info_t *ensure_family (const char *key, const char *display) {
    font_family_info_t *family = find_family_by_key (key);
    if (family)
        return family;
    if (g_family_count == g_family_cap) {
        size_t new_cap = (g_family_cap == 0) ? 8 : g_family_cap * 2;
        font_family_info_t *grown
            = (font_family_info_t *)realloc (g_families, new_cap * sizeof (*g_families));
        if (!grown)
            return NULL;
        g_families = grown;
        g_family_cap = new_cap;
    }
    family = &g_families[g_family_count++];
    memset (family, 0, sizeof (*family));
    string_copy (family->key, sizeof (family->key), key);
    string_copy (family->display, sizeof (family->display), display);
    return family;
}

/**
 * @brief Додати новий варіант шрифту у сімейство, розширивши масив.
 */
static font_variant_info_t *append_variant (font_family_info_t *family) {
    if (!family)
        return NULL;
    if (family->variant_count == family->variant_cap) {
        size_t new_cap = (family->variant_cap == 0) ? 4 : family->variant_cap * 2;
        font_variant_info_t *grown = (font_variant_info_t *)realloc (
            family->variants, new_cap * sizeof (*family->variants));
        if (!grown)
            return NULL;
        family->variants = grown;
        family->variant_cap = new_cap;
    }
    font_variant_info_t *variant = &family->variants[family->variant_count++];
    memset (variant, 0, sizeof (*variant));
    return variant;
}

/**
 * @brief Лениво побудувати каталог сімейств/варіантів із hershey/index.json.
 *
 * @return 0 при успіху; -1 якщо не вдалося зчитати дані.
 */
static int fontreg_ensure_catalog (void) {
    if (g_catalog_ready)
        return (g_family_count > 0) ? 0 : -1;

    font_face_t *faces = NULL;
    size_t face_count = 0;
    if (fontreg_list (&faces, &face_count) != 0)
        return -1;

    for (size_t i = 0; i < face_count; ++i) {
        font_face_t *face = &faces[i];
        char key[64];
        char display[96];
        derive_family_key (face, key, sizeof (key), display, sizeof (display));
        font_family_info_t *family = ensure_family (key, display);
        if (!family) {
            LOGW ("реєстр шрифтів: не вдалося створити сімʼю для %s", face->name);
            continue;
        }
        font_variant_info_t *variant = append_variant (family);
        if (!variant) {
            LOGW ("реєстр шрифтів: не вдалося додати варіант %s", face->name);
            continue;
        }
        variant->face = *face;
        variant->style_flags = style_flags_from_face (face);

        font_t *font = NULL;
        if (font_load_from_file (face->path, &font) == 0) {
            uint32_t *codes = NULL;
            size_t code_count = 0;
            if (font_list_codepoints (font, &codes, &code_count) != 0) {
                free (codes);
                codes = NULL;
                code_count = 0;
            }
            font_release (font);
            if (codes && code_count > 0) {
                qsort (codes, code_count, sizeof (*codes), cmp_uint32);
                dedupe_codepoints (codes, &code_count);
            }
            variant->codepoints = codes;
            variant->codepoint_count = code_count;
        } else {
            variant->codepoints = NULL;
            variant->codepoint_count = 0;
            LOGW ("реєстр шрифтів: не вдалося завантажити %s", face->path);
        }

        family->capability_mask |= variant->style_flags;
        if (family->display[0] == '\0')
            string_copy (family->display, sizeof (family->display), display);
    }

    free (faces);
    g_catalog_ready = 1;
    return (g_family_count > 0) ? 0 : -1;
}

/**
 * @brief Порахувати кількість встановлених бітів у масці стилів.
 */
static int popcount_int (int mask) {
    int count = 0;
    while (mask) {
        count += mask & 1;
        mask >>= 1;
    }
    return count;
}

/**
 * @brief Визначити пріоритет стилю для сортування (regular → bold → italic).
 */
static int style_priority (int flags) {
    if (flags & FONT_STYLE_REGULAR)
        return 0;
    if (flags & FONT_STYLE_BOLD)
        return 1;
    if (flags & FONT_STYLE_ITALIC)
        return 2;
    return 3;
}

/**
 * @brief Перевірити, чи увімкнено стильовий прапор у масці.
 */
static int has_style_flag (int mask, int flag) { return (mask & flag) ? 1 : 0; }

/**
 * @brief Перевірити, чи містить варіант конкретну кодову точку (бінарний пошук).
 */
static int variant_contains_codepoint (const font_variant_info_t *variant, uint32_t cp) {
    if (!variant || variant->codepoint_count == 0)
        return 0;
    size_t left = 0;
    size_t right = variant->codepoint_count;
    while (left < right) {
        size_t mid = left + (right - left) / 2;
        uint32_t value = variant->codepoints[mid];
        if (value == cp)
            return 1;
        if (value < cp)
            left = mid + 1;
        else
            right = mid;
    }
    return 0;
}

/**
 * @brief Порахувати кількість кодових точок, які покриває варіант.
 */
static size_t variant_coverage (
    const font_variant_info_t *variant, const uint32_t *codepoints, size_t codepoint_count) {
    if (!variant)
        return 0;
    size_t covered = 0;
    for (size_t i = 0; i < codepoint_count; ++i) {
        if (variant_contains_codepoint (variant, codepoints[i]))
            ++covered;
    }
    return covered;
}

/**
 * @brief Проміжні результати оцінки сімейства для підбору шрифту.
 */
typedef struct {
    const font_family_info_t *family;
    const font_variant_info_t *best_variant;
    size_t best_variant_index;
    size_t best_variant_cover;
    int best_style_flags;
    int best_priority;
    int capability_mask;
    int variant_count;
    int covers_all;
} family_eval_t;

/**
 * @brief Оцінити сімейство щодо набору кодових точок і стилів.
 */
static void evaluate_family (
    const font_family_info_t *family,
    const uint32_t *codepoints,
    size_t codepoint_count,
    family_eval_t *out) {
    memset (out, 0, sizeof (*out));
    out->family = family;
    out->best_variant_index = (size_t)-1;
    out->capability_mask = family->capability_mask;
    out->variant_count = (int)family->variant_count;

    for (size_t i = 0; i < family->variant_count; ++i) {
        const font_variant_info_t *variant = &family->variants[i];
        size_t cover = variant_coverage (variant, codepoints, codepoint_count);
        int priority = style_priority (variant->style_flags);
        int covers_all = (codepoint_count == 0) ? 1 : (cover == codepoint_count);
        int better = 0;
        if (out->best_variant_index == (size_t)-1)
            better = 1;
        else if (covers_all > out->covers_all)
            better = 1;
        else if (covers_all == out->covers_all && cover > out->best_variant_cover)
            better = 1;
        else if (
            covers_all == out->covers_all && cover == out->best_variant_cover
            && priority < out->best_priority)
            better = 1;
        else if (
            covers_all == out->covers_all && cover == out->best_variant_cover
            && priority == out->best_priority
            && has_style_flag (variant->style_flags, FONT_STYLE_REGULAR)
                   > has_style_flag (out->best_style_flags, FONT_STYLE_REGULAR))
            better = 1;
        if (better) {
            out->best_variant = variant;
            out->best_variant_index = i;
            out->best_variant_cover = cover;
            out->best_style_flags = variant->style_flags;
            out->best_priority = priority;
            out->covers_all = covers_all;
        }
    }
}

/**
 * @brief Порівняти два сімейства, коли обидва повністю покривають текст.
 */
static int family_full_better (const family_eval_t *candidate, const family_eval_t *current) {
    int cap_c = popcount_int (candidate->capability_mask);
    int cap_cur = popcount_int (current->capability_mask);
    if (cap_c != cap_cur)
        return cap_c > cap_cur;
    int reg_c = has_style_flag (candidate->capability_mask, FONT_STYLE_REGULAR);
    int reg_cur = has_style_flag (current->capability_mask, FONT_STYLE_REGULAR);
    if (reg_c != reg_cur)
        return reg_c > reg_cur;
    int bold_c = has_style_flag (candidate->capability_mask, FONT_STYLE_BOLD);
    int bold_cur = has_style_flag (current->capability_mask, FONT_STYLE_BOLD);
    if (bold_c != bold_cur)
        return bold_c > bold_cur;
    int italic_c = has_style_flag (candidate->capability_mask, FONT_STYLE_ITALIC);
    int italic_cur = has_style_flag (current->capability_mask, FONT_STYLE_ITALIC);
    if (italic_c != italic_cur)
        return italic_c > italic_cur;
    if (candidate->best_priority != current->best_priority)
        return candidate->best_priority < current->best_priority;
    if (candidate->variant_count != current->variant_count)
        return candidate->variant_count > current->variant_count;
    return strcmp (candidate->family->display, current->family->display) < 0;
}

/**
 * @brief Порівняти два сімейства при частковому покритті тексту.
 */
static int family_partial_better (const family_eval_t *candidate, const family_eval_t *current) {
    if (candidate->best_variant_cover != current->best_variant_cover)
        return candidate->best_variant_cover > current->best_variant_cover;
    int cap_c = popcount_int (candidate->capability_mask);
    int cap_cur = popcount_int (current->capability_mask);
    if (cap_c != cap_cur)
        return cap_c > cap_cur;
    int reg_c = has_style_flag (candidate->capability_mask, FONT_STYLE_REGULAR);
    int reg_cur = has_style_flag (current->capability_mask, FONT_STYLE_REGULAR);
    if (reg_c != reg_cur)
        return reg_c > reg_cur;
    if (candidate->best_priority != current->best_priority)
        return candidate->best_priority < current->best_priority;
    if (candidate->variant_count != current->variant_count)
        return candidate->variant_count > current->variant_count;
    return strcmp (candidate->family->display, current->family->display) < 0;
}

/**
 * @brief Знайти індекс сімейства, що містить варіант з указаним ID.
 */
static int
find_family_index_by_variant_id (const char *id, family_eval_t *evals, size_t eval_count) {
    if (!id)
        return -1;
    for (size_t i = 0; i < eval_count; ++i) {
        const family_eval_t *eval = &evals[i];
        for (size_t j = 0; j < eval->family->variant_count; ++j) {
            if (strcmp (eval->family->variants[j].face.id, id) == 0)
                return (int)i;
        }
    }
    return -1;
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
 * @brief Встановити базовий каталог, відносно якого шукати hershey/.
 *
 * @param path Абсолютний шлях до каталогу з ресурсами або NULL для скидання.
 */
void fontreg_set_root (const char *path) {
    fontreg_catalog_clear ();
    g_font_root_initialized = 1;
    if (!path || !*path) {
        g_font_root[0] = '\0';
        log_print (LOG_INFO, "реєстр шрифтів: скинуто базовий каталог");
        return;
    }
    string_copy (g_font_root, sizeof (g_font_root), path);
    size_t len = strlen (g_font_root);
    while (len > 0 && g_font_root[len - 1] == '/') {
        g_font_root[len - 1] = '\0';
        --len;
    }
    log_print (LOG_INFO, "реєстр шрифтів: встановлено базовий каталог → %s", g_font_root);
}

/**
 * @brief Отримати фактичний базовий каталог, враховуючи CPLOT_DATA_DIR.
 *
 * Вперше ініціалізується під час виклику, використовуючи змінну середовища або значення,
 * задане через `fontreg_set_root()`.
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
 * @brief Зчитати список усіх доступних SVG-шрифтів із `hershey/index.json`.
 *
 * Читає файл повністю та проходить по верхньорівневих ключах JSON (див. hershey/index.json),
 * використовуючи утиліти `jsr`. Результат — масив `font_face_t`, який викликач звільняє через
 * `free()`.
 *
 * @param[out] faces Масив описів (malloc).
 * @param[out] count Кількість записів.
 * @return 0 при успіху; від'ємний код при помилці I/O/парсингу.
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
            LOGW ("шлях до індексу шрифтів занадто довгий відносно бази: %s", root);
    }
    if (!fp)
        fp = fopen ("hershey/index.json", "r");
    if (!fp) {
        LOGE ("не знайдено індекс шрифтів");
        log_print (
            LOG_ERROR, "реєстр шрифтів: індекс відсутній (root=%s)",
            root ? root : "<робочий каталог>");
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
        log_print (LOG_ERROR, "реєстр шрифтів: нестача пам’яті під час читання індексу");
        return -3;
    }

    /* Ітерація по ключах верхнього рівня */
    const char *p = json_skip_ws (json);
    if (*p != '{') {
        free (arr);
        free (json);
        LOGE ("некоректний формат індексу шрифтів (очікувався об’єкт)");
        log_print (LOG_ERROR, "реєстр шрифтів: некоректний формат індексу");
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
            string_copy (arr[*count].id, sizeof (arr[*count].id), key);
            string_copy (arr[*count].name, sizeof (arr[*count].name), name);
            int written;
            if (root)
                written = snprintf (
                    arr[*count].path, sizeof (arr[*count].path), "%s/hershey/%s", root, file);
            else
                written
                    = snprintf (arr[*count].path, sizeof (arr[*count].path), "hershey/%s", file);
            if (written < 0 || (size_t)written >= sizeof (arr[*count].path)) {
                LOGW ("шлях до шрифту занадто довгий: %s", file);
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
    log_print (LOG_INFO, "реєстр шрифтів: завантажено %zu шрифтів", *count);
    free (json);
    return 0;
}

int fontreg_list_families (font_family_name_t **families, size_t *count) {
    if (!families || !count)
        return -1;
    *families = NULL;
    *count = 0;
    if (fontreg_ensure_catalog () != 0)
        return -2;
    if (g_family_count == 0)
        return 0;
    font_family_name_t *arr
        = (font_family_name_t *)calloc (g_family_count, sizeof (font_family_name_t));
    if (!arr)
        return -3;
    for (size_t i = 0; i < g_family_count; ++i) {
        string_copy (arr[i].key, sizeof (arr[i].key), g_families[i].key);
        string_copy (arr[i].name, sizeof (arr[i].name), g_families[i].display);
    }
    *families = arr;
    *count = g_family_count;
    return 0;
}

/**
 * @brief Обрати шрифт, що найкраще покриває задані кодові точки.
 *
 * @param preferred_family Бажана родина (може бути NULL).
 * @param codepoints       Масив кодових точок.
 * @param codepoint_count  Кількість кодових точок.
 * @param[out] out_face    Результат (повний опис шрифту).
 * @return 0 при успіху; 1 якщо не знайдено; -1 при помилці.
 */
int fontreg_select_face_for_codepoints (
    const char *preferred_family,
    const uint32_t *codepoints,
    size_t codepoint_count,
    font_face_t *out_face) {
    if (!out_face)
        return -1;
    if (fontreg_ensure_catalog () != 0)
        return -1;
    /* Якщо користувач явно вказав конкретний шрифт (ідентифікатор face.id),
     * використати його без подальшого підбору за родиною/стилем. Це дозволяє
     * задавати індивідуальні гарнітури в конфігурації (font_family=face_id) і
     * будувати прев’ю для кожного окремого шрифту. */
    if (preferred_family && *preferred_family) {
        font_face_t exact;
        if (fontreg_resolve (preferred_family, &exact) == 0) {
            char q[96], id[64];
            string_copy (q, sizeof (q), preferred_family);
            string_to_lower_ascii (q);
            string_copy (id, sizeof (id), exact.id);
            string_to_lower_ascii (id);
            if (strcmp (q, id) == 0) {
                *out_face = exact;
                return 0;
            }
        }
    }
    if (codepoint_count == 0)
        return fontreg_resolve (preferred_family, out_face);
    if (g_family_count == 0)
        return -1;

    family_eval_t *evals = (family_eval_t *)calloc (g_family_count, sizeof (*evals));
    if (!evals)
        return -1;

    for (size_t i = 0; i < g_family_count; ++i)
        evaluate_family (&g_families[i], codepoints, codepoint_count, &evals[i]);

    font_face_t resolved_pref;
    int preferred_index = -1;
    if (preferred_family && fontreg_resolve (preferred_family, &resolved_pref) == 0)
        preferred_index = find_family_index_by_variant_id (resolved_pref.id, evals, g_family_count);

    size_t full_best = (size_t)-1;
    size_t partial_best = (size_t)-1;
    for (size_t i = 0; i < g_family_count; ++i) {
        if (evals[i].best_variant_index == (size_t)-1)
            continue;
        if (evals[i].covers_all) {
            if (full_best == (size_t)-1 || family_full_better (&evals[i], &evals[full_best]))
                full_best = i;
        } else {
            if (partial_best == (size_t)-1
                || family_partial_better (&evals[i], &evals[partial_best]))
                partial_best = i;
        }
    }

    size_t chosen = (size_t)-1;
    if (preferred_index >= 0 && evals[preferred_index].best_variant_index != (size_t)-1) {
        const family_eval_t *pref = &evals[preferred_index];
        if (pref->covers_all) {
            if (full_best != (size_t)-1 && family_full_better (&evals[full_best], pref))
                chosen = full_best;
            else
                chosen = (size_t)preferred_index;
        } else {
            if (full_best != (size_t)-1)
                chosen = full_best;
            else if (
                partial_best != (size_t)-1 && family_partial_better (&evals[partial_best], pref))
                chosen = partial_best;
            else
                chosen = (size_t)preferred_index;
        }
    } else {
        chosen = (full_best != (size_t)-1) ? full_best : partial_best;
    }

    int rc = -1;
    if (chosen != (size_t)-1 && evals[chosen].best_variant) {
        *out_face = evals[chosen].best_variant->face;
        rc = 0;
    }

    free (evals);
    return rc;
}

/**
 * Знайти шрифт за ідентифікатором або частковим збігом імені (case-insensitive).
 *
 * Якщо @p query порожній, повертає шрифт за замовчуванням "ems_nixish".
 *
 * @param query Пошуковий рядок (може бути NULL або порожнім).
 * @param out   Вихід: знайдений шрифт.
 * @return 0 у разі успіху; -3 якщо не знайдено; інші від'ємні коди — помилки читання списку.
 */
/**
 * @brief Знайти шрифт за ідентифікатором або відображуваною назвою.
 *
 * @param query Ідентифікатор/назва (регістр не важливий).
 * @param[out] out Опис шрифту.
 * @return 0 при успіху; 1 якщо не знайдено; -1 при помилці.
 */
int fontreg_resolve (const char *query, font_face_t *out) {
    if (!out)
        return -1;
    font_face_t *faces = NULL;
    size_t count = 0;
    if (fontreg_list (&faces, &count) != 0 || count == 0) {
        free (faces);
        LOGE ("не вдалося завантажити реєстр шрифтів");
        log_print (LOG_ERROR, "реєстр шрифтів: дані недоступні");
        return -2;
    }
    /* Default to ems_nixish when query is NULL/empty */
    const char *fallback_id = "ems_nixish";
    int found = 0;
    if (query && *query) {
        char q[96];
        string_copy (q, sizeof (q), query);
        string_to_lower_ascii (q);
        for (size_t i = 0; i < count; ++i) {
            char id[64];
            string_copy (id, sizeof (id), faces[i].id);
            string_to_lower_ascii (id);
            char nm[96];
            string_copy (nm, sizeof (nm), faces[i].name);
            string_to_lower_ascii (nm);
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
        log_print (
            LOG_WARN, "реєстр шрифтів: шрифт не знайдено (%s)",
            (query && *query) ? query : "<типовий>");
        return -3;
    }
    LOGD ("вибрано шрифт: %s", out->id);
    log_print (
        LOG_INFO, "реєстр шрифтів: обрано шрифт %s (%s)", out->id,
        (query && *query) ? query : "<типовий>");
    return 0;
}
