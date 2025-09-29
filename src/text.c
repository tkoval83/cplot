/**
 * @file text.c
 * @brief Реалізація рендерингу шрифтів Hershey та верстки.
 * @ingroup text
 * @details
 * Містить розбір UTF‑8, підбір шрифтів/фолбеків, простий токенізатор тексту,
 * розстановку переносів і рендеринг рядків у контури Hershey. Всі координати
 * повертаються у вказаних одиницях (`geom_units_t`).
 */

#include "text.h"

#include "font.h"
#include "fontreg.h"
#include "glyph.h"
#include "shape.h"
#include "str.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** \brief Макрос для обчислення кількості елементів у статичному масиві. */
#ifndef ARRAY_LEN
#define ARRAY_LEN(arr) (sizeof (arr) / sizeof ((arr)[0]))
#endif

/** \brief Порівняння `uint32_t` для `qsort`. */
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
 * @brief Гарантує ємність масиву кодових точок.
 */
static int ensure_codepoints_capacity (uint32_t **arr, size_t *cap, size_t needed) {
    if (!arr || !cap)
        return -1;
    if (*cap >= needed)
        return 0;
    size_t new_cap = (*cap == 0) ? 16 : *cap;
    while (new_cap < needed)
        new_cap *= 2;
    uint32_t *grown = realloc (*arr, new_cap * sizeof (*grown));
    if (!grown)
        return -1;
    *arr = grown;
    *cap = new_cap;
    return 0;
}

/**
 * @brief Декодує UTF‑8 рядок у унікальні відсортовані кодові точки.
 * @param text Вхідний текст.
 * @param out_codes [out] Масив кодових точок (`malloc`), або `NULL` якщо порожньо.
 * @param out_count [out] Кількість елементів у `out_codes`.
 * @return 0 — успіх; -1 — помилка памʼяті/аргументів.
 */
static int collect_codepoints (const char *text, uint32_t **out_codes, size_t *out_count) {
    if (!out_codes || !out_count)
        return -1;
    *out_codes = NULL;
    *out_count = 0;
    if (!text || !*text)
        return 0;

    uint32_t *codes = NULL;
    size_t count = 0;
    size_t cap = 0;
    const char *cursor = text;
    while (*cursor) {
        uint32_t cp = 0;
        size_t consumed = 0;
        if (str_utf8_decode (cursor, &cp, &consumed) != 0 || consumed == 0) {
            ++cursor;
            continue;
        }
        if (ensure_codepoints_capacity (&codes, &cap, count + 1) != 0) {
            free (codes);
            return -1;
        }
        codes[count++] = cp;
        cursor += consumed;
    }

    if (count > 0) {
        qsort (codes, count, sizeof (*codes), cmp_uint32);
        size_t unique = 0;
        for (size_t i = 0; i < count; ++i) {
            if (i == 0 || codes[i] != codes[i - 1])
                codes[unique++] = codes[i];
        }
        count = unique;
    }

    *out_codes = codes;
    *out_count = count;
    return 0;
}

/** \brief Запис підрахунку використання гліфів родини шрифту. */
typedef struct {
    char name[96]; /**< Назва родини. */
    size_t glyphs; /**< Кількість гліфів, відрендерених цією родиною. */
} font_usage_entry_t;

/** \brief Підсумкова статистика використання родин шрифтів. */
typedef struct {
    font_usage_entry_t *entries; /**< Динамічний масив записів. */
    size_t count;                /**< Кількість елементів. */
    size_t cap;                  /**< Ємність масиву. */
} font_usage_stats_t;

/** \brief Ініціалізує структуру статистики. */
static void font_usage_stats_init (font_usage_stats_t *stats) {
    if (!stats)
        return;
    memset (stats, 0, sizeof (*stats));
}

/** \brief Вивільняє ресурси статистики. */
static void font_usage_stats_dispose (font_usage_stats_t *stats) {
    if (!stats)
        return;
    free (stats->entries);
    stats->entries = NULL;
    stats->count = 0;
    stats->cap = 0;
}

/** \brief Збільшує лічильник використання для вказаної родини. */
static int font_usage_stats_increment (font_usage_stats_t *stats, const char *name) {
    if (!stats || !name || !*name)
        return 0;
    for (size_t i = 0; i < stats->count; ++i) {
        if (strcmp (stats->entries[i].name, name) == 0) {
            stats->entries[i].glyphs++;
            return 0;
        }
    }
    if (stats->count == stats->cap) {
        size_t new_cap = (stats->cap == 0) ? 4 : stats->cap * 2;
        font_usage_entry_t *grown = realloc (stats->entries, new_cap * sizeof (*grown));
        if (!grown)
            return -1;
        stats->entries = grown;
        stats->cap = new_cap;
    }
    font_usage_entry_t *slot = &stats->entries[stats->count++];
    memset (slot, 0, sizeof (*slot));
    strncpy (slot->name, name, sizeof (slot->name) - 1);
    slot->glyphs = 1;
    return 0;
}

/** \brief Повертає родину з найбільшою кількістю гліфів. */
static const font_usage_entry_t *font_usage_stats_dominant (const font_usage_stats_t *stats) {
    if (!stats || stats->count == 0)
        return NULL;
    const font_usage_entry_t *best = &stats->entries[0];
    for (size_t i = 1; i < stats->count; ++i) {
        const font_usage_entry_t *candidate = &stats->entries[i];
        if (candidate->glyphs > best->glyphs)
            best = candidate;
        else if (candidate->glyphs == best->glyphs && strcmp (candidate->name, best->name) < 0)
            best = candidate;
    }
    return best;
}

/** \brief Загальна кількість відрендерених гліфів у статистиці. */
static size_t font_usage_stats_total (const font_usage_stats_t *stats) {
    if (!stats)
        return 0;
    size_t total = 0;
    for (size_t i = 0; i < stats->count; ++i)
        total += stats->entries[i].glyphs;
    return total;
}

/** \brief Внутрішній рядок верстки. */
typedef struct {
    char *text;
    size_t len;
    size_t cap;
    double width_units;
    bool hyphenated;
    size_t start_index;
    double offset_units;
    double baseline_units;
} layout_line_t;

/** \brief Фрагмент гліфа з шириною та кодовою точкою. */
typedef struct {
    const char *ptr;
    size_t byte_len;
    double advance_units;
    uint32_t codepoint;
    bool missing;
} glyph_segment_t;

/** \brief Тип токена простого токенізатора тексту. */
typedef enum {
    TK_WORD = 1,
    TK_SPACE = 2,
    TK_NEWLINE = 3,
} token_type_t;

/** \brief Токен рядка: слово/пробіл/перенос. */
typedef struct {
    token_type_t type;
    const char *ptr;
    size_t len;

    glyph_segment_t *segs;
    size_t seg_count;
    double width_units;
    bool ascii_only;
} text_token_t;

/** \brief Звільняє масив токенів разом із сегментами гліфів. */
static void tokens_dispose (text_token_t *toks, size_t count) {
    if (!toks)
        return;
    for (size_t i = 0; i < count; ++i)
        free (toks[i].segs);
    free (toks);
}

/** \brief Токенізує вхідний текст на слова/пропуски/переноси. */
static int tokenize_text (const char *input, text_token_t **out, size_t *out_count) {
    if (!out || !out_count)
        return -1;
    *out = NULL;
    *out_count = 0;
    if (!input)
        input = "";

    size_t cap = 0, count = 0;
    text_token_t *toks = NULL;
    const char *p = input;
    bool have_space_run = false;
    while (*p) {
        unsigned char ch = (unsigned char)*p;
        if (ch == '\r') {
            ++p;
            continue;
        }
        if (ch == '\n') {
            if (have_space_run)
                have_space_run = false;
            if (count == cap) {
                size_t nc = cap ? cap * 2 : 16;
                text_token_t *nt = realloc (toks, nc * sizeof *nt);
                if (!nt) {
                    free (toks);
                    return -1;
                }
                toks = nt;
                cap = nc;
            }
            toks[count++] = (text_token_t){ .type = TK_NEWLINE };
            ++p;
            continue;
        }
        if (ch == ' ' || ch == '\t' || ch == '\f' || ch == '\v') {
            have_space_run = true;
            ++p;
            continue;
        }

        if (have_space_run) {
            if (count == cap) {
                size_t nc = cap ? cap * 2 : 16;
                text_token_t *nt = realloc (toks, nc * sizeof *nt);
                if (!nt) {
                    free (toks);
                    return -1;
                }
                toks = nt;
                cap = nc;
            }
            toks[count++] = (text_token_t){ .type = TK_SPACE };
            have_space_run = false;
        }

        const char *start = p;
        size_t len = 0;
        while (p[len]) {
            unsigned char c = (unsigned char)p[len];
            if (c == '\n' || c == '\r' || c == ' ' || c == '\t' || c == '\f' || c == '\v')
                break;
            ++len;
        }
        if (len == 0) {
            ++p;
            continue;
        }
        if (count == cap) {
            size_t nc = cap ? cap * 2 : 16;
            text_token_t *nt = realloc (toks, nc * sizeof *nt);
            if (!nt) {
                free (toks);
                return -1;
            }
            toks = nt;
            cap = nc;
        }
        toks[count++] = (text_token_t){ .type = TK_WORD, .ptr = start, .len = len };
        p += len;
    }

    *out = toks;
    *out_count = count;
    return 0;
}

static int build_word_segments (
    const font_render_context_t *ctx,
    const char *word,
    size_t word_len,
    glyph_segment_t **segments_out,
    size_t *count_out,
    double *width_units_out,
    size_t *missing_out,
    bool *ascii_only_out);

static int
shape_measure_words (const font_render_context_t *ctx, text_token_t *toks, size_t count) {
    if (!ctx || !toks)
        return -1;
    for (size_t i = 0; i < count; ++i) {
        if (toks[i].type != TK_WORD)
            continue;
        double width = 0.0;
        bool ascii_only = true;
        glyph_segment_t *segs = NULL;
        size_t seg_count = 0;
        if (build_word_segments (
                ctx, toks[i].ptr, toks[i].len, &segs, &seg_count, &width, NULL, &ascii_only)
            != 0)
            return -1;
        toks[i].segs = segs;
        toks[i].seg_count = seg_count;
        toks[i].width_units = width;
        toks[i].ascii_only = ascii_only;
    }
    return 0;
}

typedef struct {
    char *prefix;
    size_t prefix_len;
    double prefix_width_units;
    char *suffix;
    size_t suffix_len;
    bool inserted_hyphen;
} split_result_t;

static int build_word_segments (
    const font_render_context_t *ctx,
    const char *word,
    size_t word_len,
    glyph_segment_t **segments_out,
    size_t *count_out,
    double *width_units_out,
    size_t *missing_out,
    bool *ascii_only_out);
static int split_segments (
    const glyph_segment_t *segments,
    size_t seg_count,
    double available_units,
    const font_render_context_t *ctx,
    bool ascii_only,
    bool allow_hyphenation,
    split_result_t *out);
static int force_split_segments (
    const glyph_segment_t *segments,
    size_t seg_count,
    double available_units,
    const font_render_context_t *ctx,
    split_result_t *out);

static void free_lines (layout_line_t *lines, size_t count) {
    if (!lines)
        return;
    for (size_t i = 0; i < count; ++i)
        free (lines[i].text);
    free (lines);
}

static int line_reserve (layout_line_t *line, size_t needed) {
    if (!line)
        return -1;
    if (line->cap >= needed)
        return 0;
    size_t new_cap = line->cap ? line->cap : 32;
    while (new_cap < needed)
        new_cap *= 2;
    char *grown = realloc (line->text, new_cap);
    if (!grown)
        return -1;
    line->text = grown;
    line->cap = new_cap;
    return 0;
}

static int line_append_char (layout_line_t *line, char c) {
    if (!line)
        return -1;
    if (line_reserve (line, line->len + 2) != 0)
        return -1;
    line->text[line->len++] = c;
    line->text[line->len] = '\0';
    return 0;
}

static int line_append_bytes (layout_line_t *line, const char *data, size_t len) {
    if (!line)
        return -1;
    if (len == 0)
        return 0;
    if (line_reserve (line, line->len + len + 1) != 0)
        return -1;
    memcpy (line->text + line->len, data, len);
    line->len += len;
    line->text[line->len] = '\0';
    return 0;
}

static layout_line_t *
add_new_line (layout_line_t **lines, size_t *count, size_t *cap, size_t start_index) {
    if (!lines || !count || !cap)
        return NULL;
    if (*cap <= *count) {
        size_t new_cap = (*cap == 0) ? 8 : (*cap * 2);
        layout_line_t *grown = realloc (*lines, new_cap * sizeof (*grown));
        if (!grown)
            return NULL;
        *lines = grown;
        *cap = new_cap;
    }
    layout_line_t *line = &(*lines)[(*count)++];
    line->text = NULL;
    line->len = 0;
    line->cap = 0;
    line->width_units = 0.0;
    line->hyphenated = false;
    line->offset_units = 0.0;
    line->baseline_units = 0.0;
    line->start_index = start_index;
    return line;
}

static void split_result_dispose (split_result_t *res) {
    if (!res)
        return;
    free (res->prefix);
    free (res->suffix);
    memset (res, 0, sizeof (*res));
}

static void assign_layout_positions (
    const text_layout_opts_t *opts,
    const font_render_context_t *ctx,
    layout_line_t *lines,
    size_t line_count) {
    if (!opts || !ctx || !lines)
        return;
    double line_spacing_factor = (opts->line_spacing > 0.0) ? opts->line_spacing : 1.2;
    double line_height_units = ctx->line_height_units * ctx->scale;
    double baseline_units = 0.0;
    double frame_width = opts->frame_width;
    for (size_t i = 0; i < line_count; ++i) {
        layout_line_t *line = &lines[i];
        line->baseline_units = baseline_units;
        double offset = 0.0;
        if (opts->align == TEXT_ALIGN_CENTER && frame_width > line->width_units)
            offset = (frame_width - line->width_units) / 2.0;
        else if (opts->align == TEXT_ALIGN_RIGHT && frame_width > line->width_units)
            offset = frame_width - line->width_units;
        line->offset_units = (offset > 0.0) ? offset : 0.0;
        baseline_units += line_height_units * line_spacing_factor;
    }
}

static int break_tokens_into_lines (
    const text_layout_opts_t *opts,
    const font_render_context_t *ctx,
    const text_token_t *toks,
    size_t tok_count,
    layout_line_t **lines_out,
    size_t *line_count_out) {
    if (!opts || !ctx || !lines_out || !line_count_out)
        return -1;

    layout_line_t *lines = NULL;
    size_t line_count = 0, line_cap = 0;
    size_t consumed_input_total = 0;
    size_t assigned_input_current = 0;
    layout_line_t *current = add_new_line (&lines, &line_count, &line_cap, consumed_input_total);
    if (!current)
        return -1;

    const double space_width_units = ctx->space_advance_units * ctx->scale;
    bool pending_space = false;
    bool last_break_explicit = false;

    for (size_t i = 0; i < tok_count; ++i) {
        const text_token_t *tk = &toks[i];
        if (tk->type == TK_NEWLINE) {
            consumed_input_total += assigned_input_current + 1;
            assigned_input_current = 0;
            current = add_new_line (&lines, &line_count, &line_cap, consumed_input_total);
            if (!current) {
                free_lines (lines, line_count);
                return -1;
            }
            pending_space = false;
            last_break_explicit = true;
            continue;
        }
        if (tk->type == TK_SPACE) {
            pending_space = current->len > 0 || pending_space;
            continue;
        }

        const char *pending_word_ptr = tk->ptr;
        size_t pending_word_len = tk->len;
        glyph_segment_t *pending_segments = tk->segs;
        size_t pending_seg_count = tk->seg_count;
        double pending_word_width = tk->width_units;
        bool pending_ascii_only = tk->ascii_only;
        char *owned_ptr = NULL;
        int segments_owned = 0;

        while (pending_seg_count > 0) {
            double available_units = opts->frame_width - current->width_units;
            bool insert_space = pending_space && current->len > 0;
            if (insert_space)
                available_units -= space_width_units;

            if (available_units < 0.0 && current->len > 0) {
                consumed_input_total += assigned_input_current;
                assigned_input_current = 0;
                current = add_new_line (&lines, &line_count, &line_cap, consumed_input_total);
                if (!current) {
                    if (segments_owned)
                        free (pending_segments);
                    free (owned_ptr);
                    free_lines (lines, line_count);
                    return -1;
                }
                pending_space = false;
                last_break_explicit = false;
                continue;
            }

            if (pending_word_width <= available_units || current->len == 0) {
                if (insert_space) {
                    if (line_append_char (current, ' ') != 0) {
                        if (segments_owned)
                            free (pending_segments);
                        free (owned_ptr);
                        free_lines (lines, line_count);
                        return -1;
                    }
                    current->width_units += space_width_units;
                    assigned_input_current += 1;
                }
                if (line_append_bytes (current, pending_word_ptr, pending_word_len) != 0) {
                    if (segments_owned)
                        free (pending_segments);
                    free (owned_ptr);
                    free_lines (lines, line_count);
                    return -1;
                }
                current->width_units += pending_word_width;
                assigned_input_current += pending_word_len;
                if (segments_owned) {
                    free (pending_segments);
                    segments_owned = 0;
                }
                free (owned_ptr);
                owned_ptr = NULL;
                pending_seg_count = 0;
                last_break_explicit = false;
                break;
            }

            split_result_t split;
            if (split_segments (
                    pending_segments, pending_seg_count, available_units, ctx, pending_ascii_only,
                    opts->hyphenate, &split)) {
                if (insert_space) {
                    if (line_append_char (current, ' ') != 0) {
                        split_result_dispose (&split);
                        if (segments_owned)
                            free (pending_segments);
                        free (owned_ptr);
                        free_lines (lines, line_count);
                        return -1;
                    }
                    current->width_units += space_width_units;
                    assigned_input_current += 1;
                }
                if (line_append_bytes (current, split.prefix, split.prefix_len) != 0) {
                    split_result_dispose (&split);
                    if (segments_owned)
                        free (pending_segments);
                    free (owned_ptr);
                    free_lines (lines, line_count);
                    return -1;
                }
                current->width_units += split.prefix_width_units;
                assigned_input_current += split.prefix_len;
                current->hyphenated = true;

                if (split.inserted_hyphen && assigned_input_current > 0)
                    consumed_input_total += assigned_input_current - 1;
                else
                    consumed_input_total += assigned_input_current;
                assigned_input_current = 0;
                current = add_new_line (&lines, &line_count, &line_cap, consumed_input_total);
                if (!current) {
                    split_result_dispose (&split);
                    if (segments_owned)
                        free (pending_segments);
                    free (owned_ptr);
                    free_lines (lines, line_count);
                    return -1;
                }
                pending_space = false;
                last_break_explicit = false;

                if (segments_owned) {
                    free (pending_segments);
                    segments_owned = 0;
                }
                free (owned_ptr);
                owned_ptr = NULL;

                char *next_owned = split.suffix;
                size_t next_len = split.suffix_len;
                split.suffix = NULL;
                free (split.prefix);
                split.prefix = NULL;
                split_result_dispose (&split);
                owned_ptr = next_owned;
                pending_word_ptr = owned_ptr ? owned_ptr : "";
                pending_word_len = next_len;
                if (pending_word_len == 0) {
                    pending_seg_count = 0;
                    pending_word_width = 0.0;
                    break;
                }
                if (build_word_segments (
                        ctx, pending_word_ptr, pending_word_len, &pending_segments,
                        &pending_seg_count, &pending_word_width, NULL, &pending_ascii_only)
                    != 0) {
                    free (owned_ptr);
                    free_lines (lines, line_count);
                    return -1;
                }
                segments_owned = 1;
                continue;
            }

            if (opts->break_long_words) {
                split_result_t fsplit;
                if (force_split_segments (
                        pending_segments, pending_seg_count, available_units, ctx, &fsplit)) {
                    if (insert_space) {
                        if (line_append_char (current, ' ') != 0) {
                            split_result_dispose (&fsplit);
                            if (segments_owned)
                                free (pending_segments);
                            free (owned_ptr);
                            free_lines (lines, line_count);
                            return -1;
                        }
                        current->width_units += space_width_units;
                        assigned_input_current += 1;
                    }
                    if (line_append_bytes (current, fsplit.prefix, fsplit.prefix_len) != 0) {
                        split_result_dispose (&fsplit);
                        if (segments_owned)
                            free (pending_segments);
                        free (owned_ptr);
                        free_lines (lines, line_count);
                        return -1;
                    }
                    current->width_units += fsplit.prefix_width_units;
                    assigned_input_current += fsplit.prefix_len - (fsplit.inserted_hyphen ? 1 : 0);
                    current->hyphenated = fsplit.inserted_hyphen;

                    consumed_input_total += assigned_input_current;
                    assigned_input_current = 0;
                    current = add_new_line (&lines, &line_count, &line_cap, consumed_input_total);
                    if (!current) {
                        split_result_dispose (&fsplit);
                        if (segments_owned)
                            free (pending_segments);
                        free (owned_ptr);
                        free_lines (lines, line_count);
                        return -1;
                    }
                    pending_space = false;
                    last_break_explicit = false;

                    if (segments_owned) {
                        free (pending_segments);
                        segments_owned = 0;
                    }
                    free (owned_ptr);
                    owned_ptr = NULL;

                    char *next_owned = fsplit.suffix;
                    size_t next_len = fsplit.suffix_len;
                    fsplit.suffix = NULL;
                    free (fsplit.prefix);
                    fsplit.prefix = NULL;
                    split_result_dispose (&fsplit);
                    owned_ptr = next_owned;
                    pending_word_ptr = owned_ptr ? owned_ptr : "";
                    pending_word_len = next_len;
                    if (pending_word_len == 0) {
                        pending_seg_count = 0;
                        pending_word_width = 0.0;
                        break;
                    }
                    if (build_word_segments (
                            ctx, pending_word_ptr, pending_word_len, &pending_segments,
                            &pending_seg_count, &pending_word_width, NULL, &pending_ascii_only)
                        != 0) {
                        free (owned_ptr);
                        free_lines (lines, line_count);
                        return -1;
                    }
                    segments_owned = 1;
                    continue;
                }
            }

            if (current->len > 0) {
                consumed_input_total += assigned_input_current;
                assigned_input_current = 0;
                current = add_new_line (&lines, &line_count, &line_cap, consumed_input_total);
                if (!current) {
                    if (segments_owned)
                        free (pending_segments);
                    free (owned_ptr);
                    free_lines (lines, line_count);
                    return -1;
                }
                pending_space = false;
                last_break_explicit = false;
                continue;
            }

            if (line_append_bytes (current, pending_word_ptr, pending_word_len) != 0) {
                if (segments_owned)
                    free (pending_segments);
                free (owned_ptr);
                free_lines (lines, line_count);
                return -1;
            }
            current->width_units += pending_word_width;
            consumed_input_total += assigned_input_current + pending_word_len;
            assigned_input_current = 0;
            current = add_new_line (&lines, &line_count, &line_cap, consumed_input_total);
            if (!current) {
                if (segments_owned)
                    free (pending_segments);
                free (owned_ptr);
                free_lines (lines, line_count);
                return -1;
            }
            pending_space = false;
            last_break_explicit = false;
            if (segments_owned) {
                free (pending_segments);
                segments_owned = 0;
            }
            free (owned_ptr);
            owned_ptr = NULL;
            break;
        }

        pending_space = false;
        if (segments_owned) {
            free (pending_segments);
            segments_owned = 0;
        }
        free (owned_ptr);
        owned_ptr = NULL;
        last_break_explicit = false;
    }

    consumed_input_total += assigned_input_current;

    if (line_count > 1) {
        layout_line_t *last = &lines[line_count - 1];
        if ((!last->text || last->len == 0) && !last_break_explicit) {
            free (last->text);
            line_count--;
        }
    }

    *lines_out = lines;
    *line_count_out = line_count;
    return 0;
}

static int build_word_segments (
    const font_render_context_t *ctx,
    const char *word,
    size_t word_len,
    glyph_segment_t **segments_out,
    size_t *count_out,
    double *width_units_out,
    size_t *missing_out,
    bool *ascii_only_out) {
    if (!ctx || !word || !segments_out || !count_out || !width_units_out)
        return -1;
    *segments_out = NULL;
    *count_out = 0;
    *width_units_out = 0.0;
    if (missing_out)
        *missing_out = 0;
    if (ascii_only_out)
        *ascii_only_out = true;

    if (word_len == 0)
        return 0;

    glyph_segment_t *segments = calloc (word_len, sizeof (*segments));
    if (!segments)
        return -1;

    size_t seg_count = 0;
    size_t offset = 0;
    while (offset < word_len) {
        uint32_t cp = 0;
        size_t consumed = 0;
        if (str_utf8_decode (word + offset, &cp, &consumed) != 0 || consumed == 0) {
            if (missing_out)
                (*missing_out)++;
            cp = ' ';
            consumed = 1;
        }
        if (ascii_only_out && cp >= 128)
            *ascii_only_out = false;

        const glyph_t *glyph = NULL;
        double advance_units = ctx->space_advance_units;
        bool missing = false;
        if (font_find_glyph (ctx->font, cp, &glyph) == 0 && glyph) {
            glyph_info_t info;
            if (glyph_get_info (glyph, &info) == 0)
                advance_units = info.advance_width;
            else
                missing = true;
        } else {
            missing = true;
        }
        if (missing) {
            if (missing_out)
                (*missing_out)++;
        }

        segments[seg_count].ptr = word + offset;
        segments[seg_count].byte_len = consumed;
        segments[seg_count].advance_units = advance_units;
        segments[seg_count].codepoint = cp;
        segments[seg_count].missing = missing;
        ++seg_count;
        *width_units_out += advance_units * ctx->scale;
        offset += consumed;
    }

    *segments_out = segments;
    *count_out = seg_count;
    return 0;
}

static int split_segments (
    const glyph_segment_t *segments,
    size_t seg_count,
    double available_units,
    const font_render_context_t *ctx,
    bool ascii_only,
    bool allow_hyphenation,
    split_result_t *out) {
    if (!segments || !seg_count || !ctx || !out)
        return 0;
    memset (out, 0, sizeof (*out));

    double accum_units = 0.0;
    ptrdiff_t last_hyphen_index = -1;
    double width_at_hyphen = 0.0;

    for (size_t i = 0; i < seg_count; ++i) {
        accum_units += segments[i].advance_units * ctx->scale;
        if (segments[i].codepoint == '-' && accum_units <= available_units) {
            last_hyphen_index = (ptrdiff_t)i;
            width_at_hyphen = accum_units;
        }
    }

    if (last_hyphen_index >= 0) {
        size_t prefix_bytes = 0;
        for (size_t i = 0; i <= (size_t)last_hyphen_index; ++i)
            prefix_bytes += segments[i].byte_len;
        size_t suffix_bytes = 0;
        for (size_t i = (size_t)last_hyphen_index + 1; i < seg_count; ++i)
            suffix_bytes += segments[i].byte_len;

        out->prefix = malloc (prefix_bytes + 1);
        out->suffix = malloc (suffix_bytes + 1);
        if (!out->prefix || !out->suffix) {
            split_result_dispose (out);
            return 0;
        }
        memcpy (out->prefix, segments[0].ptr, prefix_bytes);
        out->prefix[prefix_bytes] = '\0';
        if (suffix_bytes > 0)
            memcpy (out->suffix, segments[last_hyphen_index + 1].ptr, suffix_bytes);
        out->suffix[suffix_bytes] = '\0';
        out->prefix_len = prefix_bytes;
        out->suffix_len = suffix_bytes;
        out->prefix_width_units = width_at_hyphen;
        out->inserted_hyphen = false;
        return 1;
    }

    if (!allow_hyphenation || !ascii_only)
        return 0;

    double hyphen_units = ctx->hyphen_advance_units * ctx->scale;
    ptrdiff_t best_index = -1;
    double width_at_index = 0.0;
    accum_units = 0.0;
    for (size_t i = 0; i < seg_count; ++i) {
        accum_units += segments[i].advance_units * ctx->scale;
        size_t remaining = seg_count - i - 1;
        if (remaining < 3)
            continue;
        if (accum_units + hyphen_units <= available_units) {
            best_index = (ptrdiff_t)i;
            width_at_index = accum_units + hyphen_units;
        }
    }

    if (best_index < 0)
        return 0;

    size_t prefix_bytes = 0;
    for (size_t i = 0; i <= (size_t)best_index; ++i)
        prefix_bytes += segments[i].byte_len;
    size_t suffix_bytes = 0;
    for (size_t i = (size_t)best_index + 1; i < seg_count; ++i)
        suffix_bytes += segments[i].byte_len;

    out->prefix = malloc (prefix_bytes + 2);
    out->suffix = malloc (suffix_bytes + 1);
    if (!out->prefix || !out->suffix) {
        split_result_dispose (out);
        return 0;
    }
    memcpy (out->prefix, segments[0].ptr, prefix_bytes);
    out->prefix[prefix_bytes] = '-';
    out->prefix[prefix_bytes + 1] = '\0';
    if (suffix_bytes > 0)
        memcpy (out->suffix, segments[best_index + 1].ptr, suffix_bytes);
    out->suffix[suffix_bytes] = '\0';
    out->prefix_len = prefix_bytes + 1;
    out->suffix_len = suffix_bytes;
    out->prefix_width_units = width_at_index;
    out->inserted_hyphen = true;
    return 1;
}

static int force_split_segments (
    const glyph_segment_t *segments,
    size_t seg_count,
    double available_units,
    const font_render_context_t *ctx,
    split_result_t *out) {
    if (!segments || !seg_count || !ctx || !out)
        return 0;
    memset (out, 0, sizeof (*out));

    double hyphen_units = ctx->hyphen_advance_units * ctx->scale;
    double accum = 0.0;
    ptrdiff_t idx = -1;
    for (size_t i = 0; i < seg_count; ++i) {
        double adv = segments[i].advance_units * ctx->scale;
        if (accum + adv <= available_units) {
            accum += adv;
            idx = (ptrdiff_t)i;
        } else {
            break;
        }
    }
    if (idx < 0)
        return 0;

    size_t prefix_bytes = 0;
    for (size_t i = 0; i <= (size_t)idx; ++i)
        prefix_bytes += segments[i].byte_len;
    size_t suffix_bytes = 0;
    for (size_t i = (size_t)idx + 1; i < seg_count; ++i)
        suffix_bytes += segments[i].byte_len;

    int insert_hyphen = (suffix_bytes > 0) && ((accum + hyphen_units) <= available_units);

    out->prefix = (char *)malloc (prefix_bytes + (insert_hyphen ? 2 : 1));
    out->suffix = (char *)malloc (suffix_bytes + 1);
    if (!out->prefix || !out->suffix) {
        split_result_dispose (out);
        return 0;
    }
    memcpy (out->prefix, segments[0].ptr, prefix_bytes);
    if (insert_hyphen)
        out->prefix[prefix_bytes++] = '-';
    out->prefix[prefix_bytes] = '\0';
    if (suffix_bytes > 0)
        memcpy (out->suffix, segments[(size_t)idx + 1].ptr, suffix_bytes);
    out->suffix[suffix_bytes] = '\0';
    out->prefix_len = prefix_bytes;
    out->suffix_len = suffix_bytes;
    out->prefix_width_units = accum + (insert_hyphen ? hyphen_units : 0.0);
    out->inserted_hyphen = insert_hyphen;
    return 1;
}

static int render_line_text (
    const font_render_context_t *ctx,
    font_fallback_t *fallbacks,
    const char *line_text,
    double start_x_units,
    double baseline_units,
    geom_paths_t *out,
    size_t *rendered_glyphs,
    size_t *missing_glyphs) {
    if (!ctx || !line_text || !out)
        return -1;
    double pen_x = start_x_units / ctx->scale;
    double baseline = baseline_units / ctx->scale;

    const char *cursor = line_text;
    while (*cursor) {
        if (*cursor == ' ') {
            pen_x += ctx->space_advance_units;
            ++cursor;
            continue;
        }
        uint32_t cp = 0;
        size_t consumed = 0;
        if (str_utf8_decode (cursor, &cp, &consumed) != 0 || consumed == 0) {
            pen_x += ctx->space_advance_units;
            ++cursor;
            if (missing_glyphs)
                (*missing_glyphs)++;
            continue;
        }
        cursor += consumed;

        double advance_units = 0.0;
        int glyph_rc = font_emit_glyph_paths (
            ctx->font, cp, pen_x, baseline, ctx->scale, out, &advance_units);
        if (glyph_rc == 0) {
            pen_x += advance_units;
            if (rendered_glyphs)
                (*rendered_glyphs)++;
            continue;
        }
        if (glyph_rc == 1) {
            double fallback_adv = 0.0;
            int fb_rc = font_fallback_emit (
                fallbacks, ctx, cp, pen_x, baseline_units, out, &fallback_adv, NULL);
            if (fb_rc == 0) {
                pen_x += fallback_adv;
                if (rendered_glyphs)
                    (*rendered_glyphs)++;
                continue;
            }
            if (missing_glyphs)
                (*missing_glyphs)++;
            if (fb_rc < 0)
                return -1;
            pen_x += ctx->space_advance_units;
            continue;
        }
        return -1;
    }
    return 0;
}

typedef struct {
    size_t start;
    size_t length;
    unsigned flags;
} span_run_t;

typedef struct {
    bool active;
    double y_mm;
    double start_mm;
    double last_mm;
    unsigned flag;
} inline_decoration_t;

#define STYLE_STRIKE TEXT_STYLE_STRIKE
#define STYLE_UNDERLINE TEXT_STYLE_UNDERLINE

static unsigned span_flags_at_position (const span_run_t *runs, size_t run_count, size_t pos) {
    unsigned flags = 0;
    if (!runs)
        return 0;
    for (size_t i = 0; i < run_count; ++i)
        if (pos >= runs[i].start && pos < runs[i].start + runs[i].length)
            flags |= runs[i].flags;
    return flags;
}

static void inline_decoration_init (inline_decoration_t *dec, double y_mm, unsigned flag) {
    if (!dec)
        return;
    dec->active = false;
    dec->y_mm = y_mm;
    dec->start_mm = 0.0;
    dec->last_mm = 0.0;
    dec->flag = flag;
}

static void inline_decoration_start (inline_decoration_t *dec, double start_mm) {
    if (!dec || dec->active)
        return;
    dec->active = true;
    dec->start_mm = start_mm;
    dec->last_mm = start_mm;
}

static void inline_decoration_extend (inline_decoration_t *dec, double end_mm) {
    if (!dec || !dec->active)
        return;
    if (end_mm > dec->last_mm)
        dec->last_mm = end_mm;
}

static void inline_decoration_stop (inline_decoration_t *dec, geom_paths_t *out) {
    if (!dec || !dec->active)
        return;
    if (dec->last_mm > dec->start_mm) {
        geom_point_t pts[2] = { { dec->start_mm, dec->y_mm }, { dec->last_mm, dec->y_mm } };
        shape_polyline (out, pts, 2, 0);
    }
    dec->active = false;
    dec->start_mm = dec->last_mm = 0.0;
}

static void inline_decoration_flush_if_needed (
    inline_decoration_t *dec, unsigned next_flags, geom_paths_t *out) {
    if (!dec)
        return;
    if (!(next_flags & dec->flag))
        inline_decoration_stop (dec, out);
}

typedef struct {
    inline_decoration_t strike;
    inline_decoration_t underline;
} decoration_state_t;

static void decoration_state_init (decoration_state_t *state, double strike_y, double underline_y) {
    if (!state)
        return;
    inline_decoration_init (&state->strike, strike_y, STYLE_STRIKE);
    inline_decoration_init (&state->underline, underline_y, STYLE_UNDERLINE);
}

static void decoration_state_start (decoration_state_t *state, unsigned flags, double pen_mm) {
    if (!state)
        return;
    if ((flags & STYLE_STRIKE) && !state->strike.active)
        inline_decoration_start (&state->strike, pen_mm);
    if ((flags & STYLE_UNDERLINE) && !state->underline.active)
        inline_decoration_start (&state->underline, pen_mm);
}

static void decoration_state_extend (decoration_state_t *state, double pen_mm) {
    if (!state)
        return;
    inline_decoration_extend (&state->strike, pen_mm);
    inline_decoration_extend (&state->underline, pen_mm);
}

static void
decoration_state_flush (decoration_state_t *state, unsigned next_flags, geom_paths_t *out) {
    if (!state)
        return;
    inline_decoration_flush_if_needed (&state->strike, next_flags, out);
    inline_decoration_flush_if_needed (&state->underline, next_flags, out);
}

static void decoration_state_stop (decoration_state_t *state, geom_paths_t *out) {
    if (!state)
        return;
    inline_decoration_stop (&state->strike, out);
    inline_decoration_stop (&state->underline, out);
}

static int slice_spans_for_line (
    const text_span_t *spans,
    size_t span_count,
    const layout_line_t *line,
    span_run_t **out_runs,
    size_t *out_count) {
    if (!out_runs || !out_count || !line)
        return -1;
    *out_runs = NULL;
    *out_count = 0;
    span_run_t *runs = NULL;
    size_t count = 0, cap = 0;
    for (size_t r = 0; r < span_count; ++r) {
        size_t s = spans[r].start;
        size_t e = s + spans[r].length;
        size_t ls = line->start_index;
        size_t le = line->start_index + line->len;
        if (e <= ls || s >= le)
            continue;
        size_t is = (s > ls) ? (s - ls) : 0;
        size_t ie = (e < le) ? (e - ls) : (le - ls);
        if (count == cap) {
            size_t nc = cap ? cap * 2 : 4;
            span_run_t *nr = realloc (runs, nc * sizeof *nr);
            if (!nr) {
                free (runs);
                return -1;
            }
            runs = nr;
            cap = nc;
        }
        runs[count++] = (span_run_t){ .start = is, .length = (ie - is), .flags = spans[r].flags };
    }
    *out_runs = runs;
    *out_count = count;
    return 0;
}

static int render_line_text_spans (
    const font_render_context_t *ctx_regular,
    const font_render_context_t *ctx_bold,
    const font_render_context_t *ctx_italic,
    const font_render_context_t *ctx_bold_italic,
    font_fallback_t *fb_regular,
    font_fallback_t *fb_bold,
    font_fallback_t *fb_italic,
    font_fallback_t *fb_bold_italic,
    const char *line_text,
    const span_run_t *runs,
    size_t run_count,
    double start_x_units,
    double baseline_units,
    geom_paths_t *out,
    size_t *rendered_glyphs,
    size_t *missing_glyphs) {
    if (!ctx_regular || !line_text || !out)
        return -1;
    (void)fb_bold_italic;

    double pen_x_font = start_x_units / ctx_regular->scale;
    double baseline_font = baseline_units / ctx_regular->scale;

    decoration_state_t deco;
    decoration_state_init (
        &deco, baseline_units - (ctx_regular->line_height_units * ctx_regular->scale) * 0.28,
        baseline_units + (ctx_regular->line_height_units * ctx_regular->scale) * 0.08);

    const char *cursor = line_text;
    size_t pos = 0;
    while (*cursor) {
        unsigned active_flags = span_flags_at_position (runs, run_count, pos);
        decoration_state_start (&deco, active_flags, pen_x_font * ctx_regular->scale);

        if (*cursor == ' ') {
            pen_x_font += ctx_regular->space_advance_units;
            decoration_state_extend (&deco, pen_x_font * ctx_regular->scale);
            ++cursor;
            ++pos;
            unsigned next_flags = span_flags_at_position (runs, run_count, pos);
            decoration_state_flush (&deco, next_flags, out);
            continue;
        }

        uint32_t cp = 0;
        size_t consumed = 0;
        if (str_utf8_decode (cursor, &cp, &consumed) != 0 || consumed == 0) {
            pen_x_font += ctx_regular->space_advance_units;
            decoration_state_extend (&deco, pen_x_font * ctx_regular->scale);
            ++cursor;
            ++pos;
            if (missing_glyphs)
                (*missing_glyphs)++;
            unsigned next_flags = span_flags_at_position (runs, run_count, pos);
            decoration_state_flush (&deco, next_flags, out);
            continue;
        }

        const font_render_context_t *ctx = ctx_regular;
        font_fallback_t *fb = fb_regular;
        int want_bold = (active_flags & TEXT_STYLE_BOLD) != 0;
        int want_italic = (active_flags & TEXT_STYLE_ITALIC) != 0;
        if (want_bold && want_italic && ctx_bold_italic) {
            ctx = ctx_bold_italic;
            fb = fb_regular;
        } else if (want_bold && want_italic) {
            if (ctx_italic) {
                ctx = ctx_italic;
                fb = fb_italic;
            } else {
                ctx = ctx_regular;
                fb = fb_regular;
            }
        } else if (want_italic && ctx_italic) {
            ctx = ctx_italic;
            fb = fb_italic;
        } else if (want_bold && ctx_bold) {
            ctx = ctx_bold;
            fb = fb_bold;
        }

        double advance_units = 0.0;
        int glyph_rc = font_emit_glyph_paths (
            ctx->font, cp, pen_x_font, baseline_font, ctx->scale, out, &advance_units);
        if (glyph_rc == 0) {
            if ((active_flags & TEXT_STYLE_BOLD) && !want_italic
                && !(ctx == ctx_bold || ctx == ctx_bold_italic)) {
                (void)font_emit_glyph_paths (
                    ctx->font, cp, pen_x_font + 0.05, baseline_font, ctx->scale, out, NULL);
            }
            pen_x_font += advance_units;
            decoration_state_extend (&deco, pen_x_font * ctx->scale);
            if (rendered_glyphs)
                (*rendered_glyphs)++;
        } else if (glyph_rc == 1) {
            double fb_adv = 0.0;
            int fb_rc
                = font_fallback_emit (fb, ctx, cp, pen_x_font, baseline_units, out, &fb_adv, NULL);
            if (fb_rc == 0) {
                pen_x_font += fb_adv;
                decoration_state_extend (&deco, pen_x_font * ctx->scale);
                if (rendered_glyphs)
                    (*rendered_glyphs)++;
            } else {
                if (missing_glyphs)
                    (*missing_glyphs)++;
                double adv = ctx->space_advance_units;
                pen_x_font += adv;
                decoration_state_extend (&deco, pen_x_font * ctx->scale);
            }
        } else {
            return -1;
        }

        cursor += consumed;
        pos += consumed;
        unsigned next_flags = span_flags_at_position (runs, run_count, pos);
        decoration_state_flush (&deco, next_flags, out);
    }

    decoration_state_stop (&deco, out);
    return 0;
}

/**
 * @copydoc text_render_hershey
 */
int text_render_hershey (
    const char *text,
    const char *family,
    double size_pt,
    unsigned style_flags,
    geom_units_t units,
    geom_paths_t *out,
    text_render_info_t *info) {
    (void)style_flags;
    if (!out)
        return -1;

    if (!text)
        text = "";
    if (size_pt <= 0.0)
        size_pt = 14.0;

    if (geom_paths_init (out, units) != 0)
        return -1;

    uint32_t *codepoints = NULL;
    size_t codepoint_count = 0;
    if (collect_codepoints (text, &codepoints, &codepoint_count) != 0) {
        geom_paths_free (out);
        return -1;
    }

    font_face_t selected_face;
    if (fontreg_select_face_for_codepoints (family, codepoints, codepoint_count, &selected_face)
        != 0) {
        if (fontreg_resolve (family, &selected_face) != 0) {
            free (codepoints);
            geom_paths_free (out);
            return -1;
        }
    }

    font_render_context_t ctx;
    if (font_render_context_init (&ctx, &selected_face, size_pt, units) != 0) {
        free (codepoints);
        geom_paths_free (out);
        return -1;
    }

    font_fallback_t fallback;
    font_fallback_init (&fallback, family, size_pt, units);
    font_usage_stats_t usage;
    font_usage_stats_init (&usage);
    free (codepoints);

    double pen_x = 0.0;
    double pen_y = 0.0;
    size_t rendered = 0;
    size_t missing = 0;

    const char *cursor = text;
    while (*cursor) {
        if (*cursor == '\r') {
            ++cursor;
            continue;
        }
        if (*cursor == '\n') {
            pen_x = 0.0;
            pen_y += ctx.line_height_units;
            ++cursor;
            continue;
        }
        if (*cursor == '\t') {
            pen_x += 4.0 * ctx.space_advance_units;
            ++cursor;
            continue;
        }

        uint32_t cp = 0;
        size_t consumed = 0;
        if (str_utf8_decode (cursor, &cp, &consumed) != 0 || consumed == 0) {
            ++missing;
            ++cursor;
            continue;
        }
        cursor += consumed;

        double advance_units = 0.0;
        int glyph_rc
            = font_emit_glyph_paths (ctx.font, cp, pen_x, pen_y, ctx.scale, out, &advance_units);
        if (glyph_rc == 0) {
            pen_x += advance_units;
            ++rendered;
            font_usage_stats_increment (&usage, ctx.face.name);
            continue;
        }
        if (glyph_rc == 1) {
            double fallback_adv = 0.0;
            double baseline_mm = pen_y * ctx.scale;
            const char *used_family = NULL;
            int fb_rc = font_fallback_emit (
                &fallback, &ctx, cp, pen_x, baseline_mm, out, &fallback_adv, &used_family);
            if (fb_rc == 0) {
                pen_x += fallback_adv;
                ++rendered;
                if (used_family && *used_family)
                    font_usage_stats_increment (&usage, used_family);
                else
                    font_usage_stats_increment (&usage, ctx.face.name);
                continue;
            }
            if (fb_rc == 1) {
                ++missing;
                pen_x += ctx.space_advance_units;
                continue;
            }
            font_usage_stats_dispose (&usage);
            font_fallback_dispose (&fallback);
            font_render_context_dispose (&ctx);
            geom_paths_free (out);
            return -1;
        }

        font_usage_stats_dispose (&usage);
        font_fallback_dispose (&fallback);
        font_render_context_dispose (&ctx);
        geom_paths_free (out);
        return -1;
    }

    if (info) {
        memset (info, 0, sizeof (*info));
        info->size_pt = size_pt;
        info->rendered_glyphs = rendered;
        info->missing_glyphs = missing;
        info->line_height = ctx.line_height_units * ctx.scale;
        const font_usage_entry_t *dominant = font_usage_stats_dominant (&usage);
        size_t total_glyphs = font_usage_stats_total (&usage);
        if (dominant) {
            strncpy (info->resolved_family, dominant->name, sizeof (info->resolved_family) - 1);
            info->resolved_glyphs = dominant->glyphs;
        } else if (ctx.face.name[0]) {
            strncpy (info->resolved_family, ctx.face.name, sizeof (info->resolved_family) - 1);
            info->resolved_glyphs = rendered;
        }
        if (total_glyphs > 0 && info->resolved_glyphs == 0)
            info->resolved_glyphs = total_glyphs;
        if (total_glyphs == 0)
            info->resolved_glyphs = 0;
    }

    font_usage_stats_dispose (&usage);
    font_fallback_dispose (&fallback);
    font_render_context_dispose (&ctx);
    return 0;
}

/**
 * @copydoc text_layout_render
 */
int text_layout_render (
    const char *text,
    const text_layout_opts_t *opts,
    geom_paths_t *out,
    text_line_metrics_t **lines_out,
    size_t *lines_count,
    text_render_info_t *info) {
    return text_layout_render_spans (text, opts, NULL, 0, out, lines_out, lines_count, info);
}

/**
 * @copydoc text_layout_render_spans
 */
int text_layout_render_spans (
    const char *text,
    const text_layout_opts_t *opts,
    const text_span_t *spans,
    size_t span_count,
    geom_paths_t *out,
    text_line_metrics_t **lines_out,
    size_t *lines_count,
    text_render_info_t *info) {
    if (!opts || !out)
        return -1;

    double frame_width = opts->frame_width;
    if (!(frame_width > 0.0))
        return -1;

    if (geom_paths_init (out, opts->units) != 0)
        return -1;

    const char *input = text ? text : "";
    uint32_t *codepoints = NULL;
    size_t codepoint_count = 0;
    if (collect_codepoints (input, &codepoints, &codepoint_count) != 0) {
        geom_paths_free (out);
        return -1;
    }

    font_face_t selected_face;
    if (fontreg_select_face_for_codepoints (
            opts->family, codepoints, codepoint_count, &selected_face)
        != 0) {
        if (fontreg_resolve (opts->family, &selected_face) != 0) {
            free (codepoints);
            geom_paths_free (out);
            return -1;
        }
    }

    font_render_context_t ctx;
    if (font_render_context_init (&ctx, &selected_face, opts->size_pt, opts->units) != 0) {
        free (codepoints);
        geom_paths_free (out);
        return -1;
    }
    font_fallback_t fallback;
    font_fallback_init (&fallback, opts->family, opts->size_pt, opts->units);
    font_usage_stats_t usage;
    font_usage_stats_init (&usage);

#define LAYOUT_FAIL()                                                                              \
    do {                                                                                           \
        font_usage_stats_dispose (&usage);                                                         \
        font_fallback_dispose (&fallback);                                                         \
        font_render_context_dispose (&ctx);                                                        \
        geom_paths_free (out);                                                                     \
        free_lines (lines, line_count);                                                            \
        if (info)                                                                                  \
            memset (info, 0, sizeof (*info));                                                      \
        return -1;                                                                                 \
    } while (0)
    free (codepoints);

    if (info) {
        memset (info, 0, sizeof (*info));
        info->size_pt = opts->size_pt;
        info->line_height = ctx.line_height_units * ctx.scale;
    }

    layout_line_t *lines = NULL;
    size_t line_count = 0;

    text_token_t *toks = NULL;
    size_t tok_count = 0;
    if (tokenize_text (input, &toks, &tok_count) != 0)
        LAYOUT_FAIL ();

    if (shape_measure_words (&ctx, toks, tok_count) != 0) {
        tokens_dispose (toks, tok_count);
        LAYOUT_FAIL ();
    }

    if (break_tokens_into_lines (opts, &ctx, toks, tok_count, &lines, &line_count) != 0) {
        tokens_dispose (toks, tok_count);
        LAYOUT_FAIL ();
    }
    tokens_dispose (toks, tok_count);

    assign_layout_positions (opts, &ctx, lines, line_count);

    size_t rendered_glyphs = 0;
    size_t missing_glyphs = 0;
    for (size_t i = 0; i < line_count; ++i) {
        layout_line_t *line = &lines[i];
        if (!line->text || line->len == 0)
            continue;

        span_run_t *line_runs = NULL;
        size_t line_run_count = 0;
        if (slice_spans_for_line (spans, span_count, line, &line_runs, &line_run_count) != 0)
            LAYOUT_FAIL ();

        const font_render_context_t *ctx_bold = NULL, *ctx_italic = NULL, *ctx_bold_italic = NULL;
        font_render_context_t bold_ctx, italic_ctx, bold_italic_ctx;
        font_fallback_t fb_bold, fb_italic, fb_bold_italic;
        int need_bold = 0, need_italic = 0;
        for (size_t r = 0; r < line_run_count; ++r) {
            if (line_runs[r].flags & TEXT_STYLE_BOLD)
                need_bold = 1;
            if (line_runs[r].flags & TEXT_STYLE_ITALIC)
                need_italic = 1;
        }
        if (need_bold) {
            char name[128];
            snprintf (name, sizeof name, "%s Bold", ctx.face.name);
            font_face_t f;
            int have_bold = 0;
            if (fontreg_resolve (name, &f) == 0) {

                if (strcmp (f.id, ctx.face.id) != 0
                    && font_render_context_init (&bold_ctx, &f, opts->size_pt, opts->units) == 0) {
                    ctx_bold = &bold_ctx;
                    font_fallback_init (&fb_bold, name, opts->size_pt, opts->units);
                    have_bold = 1;
                }
            }
            if (!have_bold) {

                if (fontreg_resolve ("Hershey Serif Bold", &f) == 0
                    && font_render_context_init (&bold_ctx, &f, opts->size_pt, opts->units) == 0) {
                    ctx_bold = &bold_ctx;
                    font_fallback_init (&fb_bold, "Hershey Serif Bold", opts->size_pt, opts->units);
                    have_bold = 1;
                }
            }
            if (!have_bold) {
                ctx_bold = NULL;
            }
        }
        if (need_italic) {
            if (font_style_context_resolve (
                    ctx.face.name, opts->size_pt, opts->units, TEXT_STYLE_ITALIC, &italic_ctx)
                == 0) {
                ctx_italic = &italic_ctx;
                font_fallback_init (&fb_italic, italic_ctx.face.name, opts->size_pt, opts->units);
            }
        }

        if (need_bold && need_italic) {
            if (font_style_context_resolve (
                    ctx.face.name, opts->size_pt, opts->units, TEXT_STYLE_BOLD | TEXT_STYLE_ITALIC,
                    &bold_italic_ctx)
                == 0) {
                ctx_bold_italic = &bold_italic_ctx;
                font_fallback_init (
                    &fb_bold_italic, bold_italic_ctx.face.name, opts->size_pt, opts->units);
            }
        }

        int rc_render = 0;
        if (line_run_count == 0) {
            rc_render = render_line_text (
                &ctx, &fallback, line->text, line->offset_units, line->baseline_units, out,
                &rendered_glyphs, &missing_glyphs);
        } else {

            const font_render_context_t *use_bold = ctx_bold;
            const font_render_context_t *use_italic = ctx_italic;
            const font_render_context_t *use_bold_italic = ctx_bold_italic;
            font_fallback_t *fb_use_bold = need_bold ? &fb_bold : &fallback;
            font_fallback_t *fb_use_italic = need_italic ? &fb_italic : &fallback;
            font_fallback_t *fb_use_bold_italic
                = (need_bold && need_italic) ? &fb_bold_italic : &fallback;

            rc_render = render_line_text_spans (
                &ctx, use_bold, use_italic, use_bold_italic, &fallback, fb_use_bold, fb_use_italic,
                fb_use_bold_italic, line->text, line_runs, line_run_count, line->offset_units,
                line->baseline_units, out, &rendered_glyphs, &missing_glyphs);
            (void)use_bold_italic;
        }
        free (line_runs);
        if (need_bold && ctx_bold)
            font_render_context_dispose (&bold_ctx);
        if (need_bold && ctx_bold)
            font_fallback_dispose (&fb_bold);
        if (need_italic && ctx_italic) {
            font_render_context_dispose (&italic_ctx);
            font_fallback_dispose (&fb_italic);
        }
        if (need_bold && need_italic && ctx_bold_italic) {
            font_render_context_dispose (&bold_italic_ctx);
            font_fallback_dispose (&fb_bold_italic);
        }
        if (rc_render != 0)
            LAYOUT_FAIL ();
    }

    if (lines_out) {
        text_line_metrics_t *metrics = calloc (line_count, sizeof (*metrics));
        if (!metrics)
            LAYOUT_FAIL ();
        for (size_t i = 0; i < line_count; ++i) {
            metrics[i].start_index = lines[i].start_index;
            metrics[i].length = lines[i].len;
            metrics[i].width = lines[i].width_units;
            metrics[i].offset_x = lines[i].offset_units;
            metrics[i].baseline_y = lines[i].baseline_units;
            metrics[i].hyphenated = lines[i].hyphenated ? 1 : 0;
        }
        *lines_out = metrics;
    }
    if (lines_count)
        *lines_count = line_count;

    const font_usage_entry_t *dominant = font_usage_stats_dominant (&usage);
    size_t total_glyphs = font_usage_stats_total (&usage);
    if (info) {
        if (dominant) {
            strncpy (info->resolved_family, dominant->name, sizeof (info->resolved_family) - 1);
            info->resolved_glyphs = dominant->glyphs;
        } else if (ctx.face.name[0]) {
            strncpy (info->resolved_family, ctx.face.name, sizeof (info->resolved_family) - 1);
            info->resolved_glyphs = rendered_glyphs;
        }
        info->rendered_glyphs = rendered_glyphs;
        info->missing_glyphs = missing_glyphs;
        if (total_glyphs > 0 && info->resolved_glyphs == 0)
            info->resolved_glyphs = total_glyphs;
    }

    font_usage_stats_dispose (&usage);
    font_fallback_dispose (&fallback);
    font_render_context_dispose (&ctx);
    free_lines (lines, line_count);
#undef LAYOUT_FAIL
    return 0;
}

/**
 * @copydoc text_layout_free_lines
 */
void text_layout_free_lines (text_line_metrics_t *lines) { free (lines); }
