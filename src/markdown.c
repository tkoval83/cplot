/**
 * @file markdown.c
 * @brief Мінімальний розбір Markdown у контурні шляхи (geom_paths_t).
 *
 * @details
 * Реалізує спрощену підмножину Markdown без зовнішніх залежностей. На вході —
 * UTF‑8 рядок, на виході — полілінії у міліметрах (без розміщення на сторінці).
 * Підтримувані блоки (v1):
 *  - Заголовки `#`/`##`/`###` (лише зміна кеглю 24/18/14 pt)
 *  - Параграфи (порожній рядок — роздільник)
 *  - Блок‑цитати (`>`) — вертикальна лінія зліва + текст із відступом
 *  - Невпорядковані списки (`-`, `*`, `+`) з вкладеністю (кожні 2 пробіли)
 *  - Впорядковані списки (`1.`) з авто-нумерацією та вкладеністю (кожні 2 пробіли)
 *  - Інлайнові стилі: `*курсив*`, `**жирний**`, `~~закреслення~~`, `++підкреслення++`
 *
 * Розміщення (поля, орієнтація, позиція на сторінці) виконується модулем canvas
 * після побудови шляхів.
 */

#include "markdown.h"

#include "geom.h"
#include "shape.h"
#include "str.h"
#include "text.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Буфер для підготовки інлайнового тексту та стилів Markdown.
 *
 * Під час парсингу `md_inline_parse()` наповнює структуру очищеним текстом
 * без службових маркерів та масивом `text_span_t` для застосування стилів.
 */
typedef struct {
    char *text;         /**< очищений текст без маркерів */
    text_span_t *spans; /**< масив інлайнових стилів */
    size_t span_count;  /**< кількість активних спанів */
    size_t span_cap;    /**< місткість масиву спанів */
} md_inline_buffer_t;

/**
 * @brief Результат рендерингу блоку Markdown перед додаванням до полотна.
 *
 * Містить геометрію в локальних координатах та, за потреби, метрики рядків.
 */
typedef struct {
    geom_paths_t paths;         /**< відрендерені шляхи з урахуванням зсуву */
    text_line_metrics_t *lines; /**< метрики рядків (може бути NULL) */
    size_t line_count;          /**< кількість рядків */
    geom_bbox_t bbox;           /**< межі відрендерованого блоку */
} md_render_block_t;

/**
 * @brief Опис одного пункту списку Markdown.
 */
typedef struct {
    char *text;       /**< сирий текст пункту списку */
    int level;        /**< рівень вкладеності (0 = верхній) */
    const char *next; /**< позиція у вихідному документі після пункту */
} md_list_item_t;

static int render_text_block (
    const char *text,
    const markdown_opts_t *opts,
    double size_pt,
    double y_offset_mm,
    geom_paths_t *accum,
    text_render_info_t *info_out,
    double *out_height_mm);

static int count_indent_spaces (const char *line, size_t linelen);
static int is_ul_marker (char c);
static int is_ol_marker (const char *line, size_t len, size_t *digits_len);

/**
 * @brief Скидає буфер інлайнових стилів у початковий стан.
 * @param buf Структура для ініціалізації (може бути NULL).
 */
static void md_inline_buffer_init (md_inline_buffer_t *buf) {
    if (!buf)
        return;
    buf->text = NULL;
    buf->spans = NULL;
    buf->span_count = 0;
    buf->span_cap = 0;
}

/**
 * @brief Звільняє ресурси, повʼязані з буфером інлайнових стилів.
 * @param buf Структура для очищення (може бути NULL).
 */
static void md_inline_buffer_dispose (md_inline_buffer_t *buf) {
    if (!buf)
        return;
    free (buf->text);
    buf->text = NULL;
    free (buf->spans);
    buf->spans = NULL;
    buf->span_count = buf->span_cap = 0;
}

/**
 * @brief Ініціалізує блок рендерингу перед заповненням.
 * @param block Структура для скидання (може бути NULL).
 */
static void md_render_block_init (md_render_block_t *block) {
    if (!block)
        return;
    memset (block, 0, sizeof (*block));
}

/**
 * @brief Звільняє ресурси блоку рендерингу.
 * @param block Структура для очищення (може бути NULL).
 */
static void md_render_block_dispose (md_render_block_t *block) {
    if (!block)
        return;
    geom_paths_free (&block->paths);
    if (block->lines)
        text_layout_free_lines (block->lines);
    memset (block, 0, sizeof (*block));
}

/**
 * @brief Перетворює типографські пункти у міліметри.
 * @param pt Значення у пунктах (pt).
 * @return Еквівалентне значення у міліметрах.
 */
static double pt_to_mm (double pt) { return pt * (25.4 / 72.0); }

/**
 * @brief Повертає базовий кегль для рендерингу Markdown.
 * @param opts Опції рендерингу (може бути NULL).
 * @return Кегль у пунктах.
 */
static double markdown_default_font_size (const markdown_opts_t *opts) {
    if (!opts || !(opts->base_size_pt > 0.0))
        return 14.0;
    return opts->base_size_pt;
}

/**
 * @brief Обчислює базовий вертикальний відступ між блоками.
 * @param opts Опції рендерингу (може бути NULL).
 * @return Висота відступу у міліметрах.
 */
static double markdown_block_gap (const markdown_opts_t *opts) {
    double base = markdown_default_font_size (opts);
    return pt_to_mm (base * 0.5);
}

/**
 * @brief Перевіряє, чи складається рядок лише з пробільних символів.
 * @param line Початок рядка.
 * @param len  Довжина рядка.
 * @return true, якщо рядок порожній або містить лише пробіли.
 */
static bool markdown_is_blank_line (const char *line, size_t len) {
    if (!line)
        return true;
    for (size_t i = 0; i < len; ++i) {
        char c = line[i];
        if (!(c == ' ' || c == '\t' || c == '\r'))
            return false;
    }
    return true;
}

/**
 * @brief Додає копію шляхів із одного контейнера до іншого.
 * @param dst Місце призначення.
 * @param src Джерело шляхів.
 * @return 0 при успіху; 1 при помилці памʼяті.
 */
static int paths_append (geom_paths_t *dst, const geom_paths_t *src) {
    if (!dst || !src)
        return 1;
    for (size_t i = 0; i < src->len; ++i) {
        const geom_path_t *p = &src->items[i];
        if (p->len == 0 || !p->pts)
            continue;
        if (geom_paths_push_path (dst, p->pts, p->len) != 0)
            return 1;
    }
    return 0;
}

/**
 * @brief Тимчасовий запис для стеку інлайнових маркерів.
 */
typedef struct {
    unsigned flag; /**< Прапорець стилю, який було відкрито. */
    size_t start;  /**< Позиція у вихідному тексті, де стиль почав діяти. */
} md_marker_t;

/**
 * @brief Додає інлайновий спан у буфер розмітки.
 * @param buf   Цільовий буфер.
 * @param start Початок діапазону у вихідному тексті.
 * @param end   Кінець діапазону (невключно).
 * @param flag  Прапорець стилю (TEXT_STYLE_*).
 * @return 0 при успіху; 1 при помилці памʼяті.
 */
static int md_inline_emit_span (md_inline_buffer_t *buf, size_t start, size_t end, unsigned flag) {
    if (!buf || start >= end)
        return 0;
    if (buf->span_count == buf->span_cap) {
        size_t new_cap = buf->span_cap ? buf->span_cap * 2 : 8;
        text_span_t *grown = (text_span_t *)realloc (buf->spans, new_cap * sizeof (*grown));
        if (!grown)
            return 1;
        buf->spans = grown;
        buf->span_cap = new_cap;
    }
    buf->spans[buf->span_count++] = (text_span_t){ start, end - start, flag };
    return 0;
}

/**
 * @brief Обробляє відкриття/закриття інлайнового стилю.
 * @param buf         Буфер для запису спанів.
 * @param stack       Стек відкритих стилів.
 * @param sp          Поточна глибина стеку.
 * @param flag        Прапорець стилю (TEXT_STYLE_*).
 * @param current_len Поточна довжина очищеного тексту.
 * @return 0 при успіху; 1 при помилці памʼяті під час додавання спану.
 */
static int md_inline_toggle (
    md_inline_buffer_t *buf, md_marker_t *stack, int *sp, unsigned flag, size_t current_len) {
    if (!buf || !stack || !sp)
        return 1;
    if (*sp > 0 && stack[*sp - 1].flag == flag) {
        md_marker_t marker = stack[--(*sp)];
        return md_inline_emit_span (buf, marker.start, current_len, flag & 0xFFFFu);
    }
    if (*sp >= 16)
        return 0; /* ігноруємо надлишок вкладеності */
    stack[(*sp)++] = (md_marker_t){ flag, current_len };
    return 0;
}

/**
 * @brief Розбирає інлайнові маркери Markdown у текст і спани.
 * @param input Вхідний текст.
 * @param out   Буфер для результату (ініціалізується усередині).
 * @return 0 при успіху; 1 при помилці памʼяті.
 */
static int md_inline_parse (const char *input, md_inline_buffer_t *out) {
    if (!out)
        return 1;
    md_inline_buffer_init (out);
    if (!input)
        input = "";

    size_t in_len = strlen (input);
    out->text = (char *)malloc (in_len + 1);
    if (!out->text)
        return 1;

    md_marker_t stack[16];
    int sp = 0;
    size_t out_len = 0;

    for (size_t i = 0; input[i];) {
        char c = input[i];
        if (c == '\\' && input[i + 1]) {
            out->text[out_len++] = input[i + 1];
            i += 2;
            continue;
        }
        if (c == '~' && input[i + 1] == '~') {
            if (md_inline_toggle (out, stack, &sp, TEXT_STYLE_STRIKE, out_len) != 0)
                return 1;
            i += 2;
            continue;
        }
        if (c == '+' && input[i + 1] == '+') {
            if (md_inline_toggle (out, stack, &sp, TEXT_STYLE_UNDERLINE, out_len) != 0)
                return 1;
            i += 2;
            continue;
        }
        if ((c == '*' && input[i + 1] == '*') || (c == '_' && input[i + 1] == '_')) {
            if (md_inline_toggle (out, stack, &sp, TEXT_STYLE_BOLD, out_len) != 0)
                return 1;
            i += 2;
            continue;
        }
        if (c == '*' || c == '_') {
            if (md_inline_toggle (out, stack, &sp, TEXT_STYLE_ITALIC, out_len) != 0)
                return 1;
            ++i;
            continue;
        }
        out->text[out_len++] = c;
        ++i;
    }
    out->text[out_len] = '\0';
    return 0;
}

/**
 * @brief Рендерить очищений текст Markdown із заданими спанами у шляхи.
 * @param opts            Опції рендерингу (може бути NULL).
 * @param inline_buf      Буфер із текстом та спанами.
 * @param size_pt         Кегль тексту.
 * @param frame_width_mm  Доступна ширина кадру.
 * @param x_offset_mm     Горизонтальний зсув перед додаванням до полотна.
 * @param y_offset_mm     Вертикальний зсув перед додаванням до полотна.
 * @param want_lines      Чи потрібні метрики рядків для подальших обчислень.
 * @param info            Необовʼязково: збирає статистику рендеру.
 * @param out_block       Результат з геометрією.
 * @return 0 при успіху; 2 при помилці рендерингу.
 */
static int md_render_inline_block (
    const markdown_opts_t *opts,
    const md_inline_buffer_t *inline_buf,
    double size_pt,
    double frame_width_mm,
    double x_offset_mm,
    double y_offset_mm,
    bool want_lines,
    text_align_t align,
    bool force_break,
    text_render_info_t *info,
    md_render_block_t *out_block) {
    if (!inline_buf || !out_block)
        return 1;

    md_render_block_init (out_block);

    text_layout_opts_t layout_opts = {
        .family = opts ? opts->family : NULL,
        .size_pt = (size_pt > 0.0) ? size_pt : markdown_default_font_size (opts),
        .style_flags = TEXT_STYLE_NONE,
        .units = GEOM_UNITS_MM,
        .frame_width = frame_width_mm,
        .align = align,
        .hyphenate = 1,
        .line_spacing = 1.0,
        .break_long_words = force_break ? 1 : 0,
    };

    geom_paths_t layout_paths;
    text_line_metrics_t *lines_local = NULL;
    size_t line_count_local = 0;
    text_line_metrics_t **lines_ptr = want_lines ? &lines_local : NULL;
    size_t *line_count_ptr = want_lines ? &line_count_local : NULL;

    if (text_layout_render_spans (
            inline_buf->text ? inline_buf->text : "", &layout_opts, inline_buf->spans,
            inline_buf->span_count, &layout_paths, lines_ptr, line_count_ptr, info)
        != 0)
        return 2;

    if (geom_paths_translate (&layout_paths, x_offset_mm, y_offset_mm, &out_block->paths) != 0) {
        geom_paths_free (&layout_paths);
        if (lines_local)
            text_layout_free_lines (lines_local);
        return 2;
    }
    geom_paths_free (&layout_paths);

    if (want_lines) {
        out_block->lines = lines_local;
        out_block->line_count = line_count_local;
    } else if (lines_local) {
        text_layout_free_lines (lines_local);
    }

    if (geom_bbox_of_paths (&out_block->paths, &out_block->bbox) != 0)
        memset (&out_block->bbox, 0, sizeof (out_block->bbox));

    return 0;
}

/**
 * @brief Повертає висоту рядка для відрендерованого тексту.
 * @param info        Статистика рендеру (може бути NULL).
 * @param fallback_pt Кегль, який використати, якщо статистика відсутня.
 * @return Висота рядка у міліметрах.
 */
static double markdown_line_height (const text_render_info_t *info, double fallback_pt) {
    if (info && info->line_height > 0.0)
        return info->line_height;
    return pt_to_mm (fallback_pt);
}

/**
 * @brief Оцінює вертикальні межі першого рядка блоку тексту.
 * @param paths            Відрендеровані шляхи.
 * @param lines            Метрики рядків (може бути NULL).
 * @param line_count       Кількість рядків у метриках.
 * @param y_offset_mm      Початковий вертикальний зсув блоку.
 * @param line_height_mm   Типова висота рядка.
 * @param cutoff_adjust_mm Зсув, що обмежує включення наступних рядків.
 * @param top_out          [out] Верхня межа першого рядка.
 * @param bottom_out       [out] Нижня межа першого рядка.
 */
static void markdown_first_line_bounds (
    const geom_paths_t *paths,
    const text_line_metrics_t *lines,
    size_t line_count,
    double y_offset_mm,
    double line_height_mm,
    double cutoff_adjust_mm,
    double *top_out,
    double *bottom_out) {
    if (top_out)
        *top_out = 0.0;
    if (bottom_out)
        *bottom_out = 0.0;
    if (!paths || paths->len == 0 || !top_out || !bottom_out)
        return;

    double cutoff = 1e9;
    if (line_count > 1)
        cutoff = y_offset_mm + lines[1].baseline_y - cutoff_adjust_mm;

    double top = 1e9;
    double bottom = -1e9;
    for (size_t pi = 0; pi < paths->len; ++pi) {
        const geom_path_t *path = &paths->items[pi];
        if (!path->pts)
            continue;
        for (size_t qi = 0; qi < path->len; ++qi) {
            double y = path->pts[qi].y;
            if (line_count > 1 && y > cutoff)
                continue;
            if (y < top)
                top = y;
            if (y > bottom)
                bottom = y;
        }
    }

    if (top <= bottom) {
        *top_out = top;
        *bottom_out = bottom;
    } else {
        *top_out = y_offset_mm;
        *bottom_out = y_offset_mm + line_height_mm;
    }
}

/**
 * @brief Збирає послідовність рядків із заданим префіксним маркером.
 * @param p         Початкове положення в тексті.
 * @param marker    Символ-маркер (наприклад, '>').
 * @param out_text  [out] Зконкатенований вміст без маркерів (може бути NULL).
 * @param out_next  [out] Позиція після останнього рядка блоку (може бути NULL).
 * @return 1, якщо принаймні один рядок зібрано; 0, якщо маркер не знайдено; 2 при помилці памʼяті.
 */
static int markdown_collect_prefixed_lines (
    const char *p, char marker, char **out_text, const char **out_next) {
    if (out_text)
        *out_text = NULL;
    if (out_next)
        *out_next = p;
    if (!p)
        return 0;

    const char *scan = p;
    size_t total = 0;
    bool found = false;

    while (*scan) {
        const char *line_end = strchr (scan, '\n');
        size_t line_len = line_end ? (size_t)(line_end - scan) : strlen (scan);
        const char *cursor = scan;
        while (*cursor == ' ' || *cursor == '\t')
            ++cursor;
        if (*cursor != marker)
            break;
        found = true;
        ++cursor;
        if (*cursor == ' ')
            ++cursor;
        total += (size_t)(line_len - (cursor - scan)) + 1;
        scan = line_end ? (line_end + 1) : (scan + line_len);
        if (*scan == '\n') {
            ++scan;
            break;
        }
    }

    if (!found)
        return 0;

    char *buffer = (char *)malloc (total + 1);
    if (!buffer)
        return 2;

    size_t blen = 0;
    const char *cursor = p;
    while (cursor < scan) {
        const char *line_end = strchr (cursor, '\n');
        size_t line_len = line_end ? (size_t)(line_end - cursor) : strlen (cursor);
        const char *content = cursor;
        while (*content == ' ' || *content == '\t')
            ++content;
        if (*content != marker)
            break;
        ++content;
        if (*content == ' ')
            ++content;
        size_t copy_len = (size_t)(line_len - (content - cursor));
        memcpy (buffer + blen, content, copy_len);
        blen += copy_len;
        buffer[blen++] = '\n';
        cursor = line_end ? (line_end + 1) : (cursor + line_len);
        if (*cursor == '\n') {
            ++cursor;
            break;
        }
    }
    if (blen > 0)
        buffer[blen - 1] = '\0';
    else
        buffer[0] = '\0';

    if (out_text)
        *out_text = buffer;
    else
        free (buffer);
    if (out_next)
        *out_next = scan;
    return out_text ? 1 : 0;
}

/* ---- Таблиці (GFM) -------------------------------------------------------- */

/**
 * @brief Розбити рядок таблиці на осередки за роздільником '|'.
 *
 * Ігнорує початковий та кінцевий символ '|' (за наявності), обрізає пробіли
 * з обох країв осередків.
 *
 * @param line       Вхідний рядок.
 * @param len        Довжина рядка.
 * @param cells_out  [out] Масив рядків-осередків (звільняє викликач через table_free_cells()).
 * @param count_out  [out] Кількість осередків.
 * @return 0 при успіху; 1 при помилці памʼяті/аргументів.
 */
static int table_split_cells (const char *line, size_t len, char ***cells_out, size_t *count_out) {
    if (!line || !cells_out || !count_out)
        return 1;
    *cells_out = NULL;
    *count_out = 0;
    if (len == 0)
        return 0;
    const char *start = line;
    const char *end = line + len;
    while (start < end && (*start == ' ' || *start == '\t'))
        ++start;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
        --end;
    if (start < end && *start == '|')
        ++start;
    if (end > start && end[-1] == '|')
        --end;
    /* Порахувати кількість розділювачів. */
    size_t parts = 1;
    for (const char *p = start; p < end; ++p)
        if (*p == '|')
            parts++;
    char **cells = (char **)calloc (parts, sizeof (*cells));
    if (!cells)
        return 1;
    size_t count = 0;
    const char *cur = start;
    while (cur <= end) {
        const char *sep = cur;
        while (sep < end && *sep != '|')
            ++sep;
        size_t slen = (size_t)(sep - cur);
        char *cell = (char *)malloc (slen + 1);
        if (!cell) {
            for (size_t i = 0; i < count; ++i)
                free (cells[i]);
            free (cells);
            return 1;
        }
        memcpy (cell, cur, slen);
        cell[slen] = '\0';
        string_trim_ascii (cell);
        cells[count++] = cell;
        if (sep >= end)
            break;
        cur = sep + 1;
    }
    *cells_out = cells;
    *count_out = count;
    return 0;
}

/**
 * @brief Звільнити масив осередків таблиці.
 */
static void table_free_cells (char **cells, size_t count) {
    if (!cells)
        return;
    for (size_t i = 0; i < count; ++i)
        free (cells[i]);
    free (cells);
}

/**
 * @brief Перевірити, чи рядок є роздільником таблиці та зчитати вирівнювання.
 *
 * Синтаксис стовпця: `---`, `:---`, `---:`, `:---:` (мінімум 3 дефіси).
 * Стовпці розділяються `|`, дозволені пробіли по краях.
 *
 * @param line      Рядок для аналізу.
 * @param len       Довжина рядка.
 * @param align_out [out] Масив вирівнювань для стовпців (0=left,1=center,2=right), може бути NULL.
 * @param cols_out  [out] Кількість стовпців (може бути NULL).
 * @return 1 якщо це валідний роздільник; 0 інакше.
 */
static int
table_is_separator_line (const char *line, size_t len, int *align_out, size_t *cols_out) {
    if (!line || len == 0)
        return 0;
    char **cells = NULL;
    size_t count = 0;
    if (table_split_cells (line, len, &cells, &count) != 0)
        return 0;
    if (count == 0) {
        table_free_cells (cells, count);
        return 0;
    }
    int ok = 1;
    if (align_out) {
        /* Очікуємо, що align_out має місткість >= count; викликаючий код забезпечує. */
    }
    for (size_t i = 0; i < count; ++i) {
        char *c = cells[i];
        size_t n = strlen (c);
        if (n < 3) {
            ok = 0;
            break;
        }
        size_t j = 0;
        int left_colon = 0, right_colon = 0;
        if (c[j] == ':') {
            left_colon = 1;
            ++j;
        }
        size_t dash = 0;
        while (j < n && c[j] == '-') {
            ++dash;
            ++j;
        }
        if (dash < 3) {
            ok = 0;
            break;
        }
        if (j < n && c[j] == ':') {
            right_colon = 1;
            ++j;
        }
        while (j < n && (c[j] == '-' || c[j] == ' '))
            ++j;
        if (j != n) {
            ok = 0;
            break;
        }
        if (align_out) {
            int a = 0;
            if (left_colon && right_colon)
                a = 1; /* center */
            else if (right_colon)
                a = 2; /* right */
            else
                a = 0; /* left */
            align_out[i] = a;
        }
    }
    if (cols_out)
        *cols_out = count;
    table_free_cells (cells, count);
    return ok;
}

/**
 * @brief Розібрати та відрендерити таблицю Markdown (GFM).
 *
 * Формат очікується як мінімум з двома рядками: заголовок і роздільник,
 * далі — рядки даних до першого порожнього або невідповідного рядка.
 */
static int markdown_try_table (
    const char *p,
    const markdown_opts_t *opts,
    double *y_offset,
    geom_paths_t *out,
    const char **p_out) {
    if (p_out)
        *p_out = p;
    if (!p || !opts || !y_offset || !out)
        return 0;

    const char *line1_end = strchr (p, '\n');
    size_t line1_len = line1_end ? (size_t)(line1_end - p) : strlen (p);
    if (line1_len == 0)
        return 0;
    if (!memchr (p, '|', line1_len))
        return 0;

    const char *line2 = line1_end ? (line1_end + 1) : (p + line1_len);
    if (!*line2)
        return 0;
    const char *line2_end = strchr (line2, '\n');
    size_t line2_len = line2_end ? (size_t)(line2_end - line2) : strlen (line2);
    int align_tmp[128];
    size_t sep_cols = 0;
    if (!table_is_separator_line (line2, line2_len, align_tmp, &sep_cols))
        return 0;

    /* Парсимо заголовок. */
    char **head_cells = NULL;
    size_t head_cols = 0;
    if (table_split_cells (p, line1_len, &head_cells, &head_cols) != 0)
        return 2;
    size_t cols = head_cols > sep_cols ? head_cols : sep_cols;
    if (cols == 0) {
        table_free_cells (head_cells, head_cols);
        return 0;
    }
    int *align = (int *)calloc (cols, sizeof (*align));
    if (!align) {
        table_free_cells (head_cells, head_cols);
        return 2;
    }
    for (size_t i = 0; i < cols; ++i)
        align[i] = (i < sep_cols) ? align_tmp[i] : 0;

    /* Збираємо рядки даних. */
    const char *cursor = line2_end ? (line2_end + 1) : (line2 + line2_len);
    size_t rows_cap = 8, rows_len = 0;
    char ***rows = (char ***)calloc (rows_cap, sizeof (*rows));
    size_t *row_counts = (size_t *)calloc (rows_cap, sizeof (*row_counts));
    if (!rows || !row_counts) {
        free (rows);
        free (row_counts);
        table_free_cells (head_cells, head_cols);
        free (align);
        return 2;
    }
    while (*cursor) {
        const char *le = strchr (cursor, '\n');
        size_t ll = le ? (size_t)(le - cursor) : strlen (cursor);
        if (ll == 0 || markdown_is_blank_line (cursor, ll))
            break;
        if (!memchr (cursor, '|', ll))
            break;
        char **cells = NULL;
        size_t cc = 0;
        if (table_split_cells (cursor, ll, &cells, &cc) != 0) {
            /* cleanup */
            for (size_t r = 0; r < rows_len; ++r)
                table_free_cells (rows[r], row_counts[r]);
            free (rows);
            free (row_counts);
            table_free_cells (head_cells, head_cols);
            free (align);
            return 2;
        }
        if (rows_len == rows_cap) {
            size_t nc = rows_cap * 2;
            char ***nr = (char ***)realloc (rows, nc * sizeof (*nr));
            size_t *nrc = (size_t *)realloc (row_counts, nc * sizeof (*nrc));
            if (!nr || !nrc) {
                free (nr);
                free (nrc);
                table_free_cells (cells, cc);
                for (size_t r = 0; r < rows_len; ++r)
                    table_free_cells (rows[r], row_counts[r]);
                free (rows);
                free (row_counts);
                table_free_cells (head_cells, head_cols);
                free (align);
                return 2;
            }
            rows = nr;
            row_counts = nrc;
            rows_cap = nc;
        }
        rows[rows_len] = cells;
        row_counts[rows_len] = cc;
        rows_len++;
        cursor = le ? (le + 1) : (cursor + ll);
    }

    /* Візуалізувати таблицю. */
    const double padding = 1.5; /* мм */
    const double frame_w = opts->frame_width_mm;
    double col_w = frame_w / (double)cols;
    /* Без додаткового відступу зверху: покладаємось на попередній блок.
       Внизу додаємо стандартний block gap для симетрії з попереднім параграфом. */
    double base_line_mm = pt_to_mm (markdown_default_font_size (opts));
    double y = *y_offset;

    /* Рядок заголовка: 2 проходи — виміряти, потім відцентрувати. */
    {
        double max_h = 0.0;
        for (size_t ci = 0; ci < cols; ++ci) {
            const char *text = (ci < head_cols) ? head_cells[ci] : "";
            md_inline_buffer_t ib;
            md_inline_buffer_init (&ib);
            if (md_inline_parse (text, &ib) == 0) {
                double x = (double)ci * col_w + padding;
                double fw = col_w - 2.0 * padding;
                if (!(fw > 4.0))
                    fw = 4.0;
                md_render_block_t blk;
                text_render_info_t ci_info;
                if (md_render_inline_block (
                        opts, &ib, markdown_default_font_size (opts), fw, x, y, true,
                        (align[ci] == 1)   ? TEXT_ALIGN_CENTER
                        : (align[ci] == 2) ? TEXT_ALIGN_RIGHT
                                           : TEXT_ALIGN_LEFT,
                        true, &ci_info, &blk)
                    == 0) {
                    double h = blk.bbox.max_y - blk.bbox.min_y;
                    if (h > max_h)
                        max_h = h;
                    md_render_block_dispose (&blk);
                }
            }
            md_inline_buffer_dispose (&ib);
        }
        double min_row_h = pt_to_mm (markdown_default_font_size (opts)) + 2.0 * padding;
        double row_h = max_h + 2.0 * padding;
        if (!(row_h > min_row_h))
            row_h = min_row_h;

        for (size_t ci = 0; ci < cols; ++ci) {
            const char *text = (ci < head_cols) ? head_cells[ci] : "";
            md_inline_buffer_t ib;
            md_inline_buffer_init (&ib);
            if (md_inline_parse (text, &ib) == 0) {
                double x = (double)ci * col_w + padding;
                double fw = col_w - 2.0 * padding;
                if (!(fw > 4.0))
                    fw = 4.0;
                md_render_block_t blk;
                text_render_info_t ci_info;
                if (md_render_inline_block (
                        opts, &ib, markdown_default_font_size (opts), fw, x, y, true,
                        (align[ci] == 1)   ? TEXT_ALIGN_CENTER
                        : (align[ci] == 2) ? TEXT_ALIGN_RIGHT
                                           : TEXT_ALIGN_LEFT,
                        true, &ci_info, &blk)
                    == 0) {
                    double desired_top = y + padding; /* верхній відступ фіксований */
                    double dy = desired_top - blk.bbox.min_y;
                    geom_paths_t shifted;
                    if (geom_paths_translate (&blk.paths, 0.0, dy, &shifted) == 0) {
                        (void)paths_append (out, &shifted);
                        geom_paths_free (&shifted);
                    }
                    md_render_block_dispose (&blk);
                }
            }
            md_inline_buffer_dispose (&ib);
        }
        for (size_t ci = 0; ci < cols; ++ci) {
            double x0 = (double)ci * col_w;
            (void)shape_rect (out, x0, y, col_w, row_h);
        }
        y += row_h;
    }

    /* Рядки даних. */
    for (size_t r = 0; r < rows_len; ++r) {
        char **cells = rows[r];
        size_t cc = row_counts[r];
        double max_h = 0.0;
        for (size_t ci = 0; ci < cols; ++ci) {
            const char *text = (ci < cc) ? cells[ci] : "";
            md_inline_buffer_t ib;
            md_inline_buffer_init (&ib);
            if (md_inline_parse (text, &ib) == 0) {
                double x = (double)ci * col_w + padding;
                double fw = col_w - 2.0 * padding;
                if (!(fw > 4.0))
                    fw = 4.0;
                md_render_block_t blk;
                text_render_info_t ci_info;
                if (md_render_inline_block (
                        opts, &ib, markdown_default_font_size (opts), fw, x, y, true,
                        (align[ci] == 1)   ? TEXT_ALIGN_CENTER
                        : (align[ci] == 2) ? TEXT_ALIGN_RIGHT
                                           : TEXT_ALIGN_LEFT,
                        true, &ci_info, &blk)
                    == 0) {
                    double h = blk.bbox.max_y - blk.bbox.min_y;
                    if (h > max_h)
                        max_h = h;
                    md_render_block_dispose (&blk);
                }
            }
            md_inline_buffer_dispose (&ib);
        }
        double min_row_h = pt_to_mm (markdown_default_font_size (opts)) + 2.0 * padding;
        double row_h = max_h + 2.0 * padding;
        if (!(row_h > min_row_h))
            row_h = min_row_h;

        for (size_t ci = 0; ci < cols; ++ci) {
            const char *text = (ci < cc) ? cells[ci] : "";
            md_inline_buffer_t ib;
            md_inline_buffer_init (&ib);
            if (md_inline_parse (text, &ib) == 0) {
                double x = (double)ci * col_w + padding;
                double fw = col_w - 2.0 * padding;
                if (!(fw > 4.0))
                    fw = 4.0;
                md_render_block_t blk;
                text_render_info_t ci_info;
                if (md_render_inline_block (
                        opts, &ib, markdown_default_font_size (opts), fw, x, y, true,
                        (align[ci] == 1)   ? TEXT_ALIGN_CENTER
                        : (align[ci] == 2) ? TEXT_ALIGN_RIGHT
                                           : TEXT_ALIGN_LEFT,
                        true, &ci_info, &blk)
                    == 0) {
                    double desired_top = y + padding; /* верхній відступ фіксований */
                    double dy = desired_top - blk.bbox.min_y;
                    geom_paths_t shifted;
                    if (geom_paths_translate (&blk.paths, 0.0, dy, &shifted) == 0) {
                        (void)paths_append (out, &shifted);
                        geom_paths_free (&shifted);
                    }
                    md_render_block_dispose (&blk);
                }
            }
            md_inline_buffer_dispose (&ib);
        }
        for (size_t ci = 0; ci < cols; ++ci) {
            double x0 = (double)ci * col_w;
            (void)shape_rect (out, x0, y, col_w, row_h);
        }
        y += row_h;
    }

    /* Додатковий відступ після таблиці для візуального «дихання» */
    /* Відступ після таблиці: повний рядок (baseline) */
    *y_offset = y + base_line_mm;
    if (p_out)
        *p_out = cursor;

    /* Прибирання. */
    table_free_cells (head_cells, head_cols);
    for (size_t r = 0; r < rows_len; ++r)
        table_free_cells (rows[r], row_counts[r]);
    free (rows);
    free (row_counts);
    free (align);
    return 1;
}

/**
 * @brief Очищає структуру пункту списку.
 * @param item Пункт для очищення (може бути NULL).
 */
static void md_list_item_dispose (md_list_item_t *item) {
    if (!item)
        return;
    free (item->text);
    item->text = NULL;
    item->level = 0;
    item->next = NULL;
}

/**
 * @brief Зчитує один пункт невпорядкованого списку.
 * @param cursor   Поточна позиція у вихідному тексті.
 * @param item     [out] Опис пункту.
 * @param out_next [out] Початок наступного рядка після пункту (може бути NULL).
 * @return 1, якщо пункт зчитано; 0, якщо маркер не знайдено; 2 при помилці памʼяті.
 */
static int markdown_read_ul_item (const char *cursor, md_list_item_t *item, const char **out_next) {
    if (out_next)
        *out_next = cursor;
    if (!cursor || !item)
        return 0;

    const char *line_end = strchr (cursor, '\n');
    size_t line_len = line_end ? (size_t)(line_end - cursor) : strlen (cursor);
    if (line_len == 0 || markdown_is_blank_line (cursor, line_len))
        return 0;

    int indent = count_indent_spaces (cursor, line_len);
    const char *marker = cursor + indent;
    if (!(line_len > (size_t)indent + 1 && is_ul_marker (*marker) && marker[1] == ' '))
        return 0;

    const char *content = marker + 2;
    size_t content_len = (size_t)(line_len - (content - cursor));
    char *text = (char *)malloc (content_len + 1);
    if (!text)
        return 2;
    memcpy (text, content, content_len);
    text[content_len] = '\0';

    md_list_item_dispose (item);
    item->text = text;
    item->level = indent / 2;
    if (item->level < 0)
        item->level = 0;
    if (item->level > 31)
        item->level = 31;
    item->next = line_end ? (line_end + 1) : (cursor + line_len);
    if (out_next)
        *out_next = item->next;
    return 1;
}

/**
 * @brief Додає до полотна один пункт невпорядкованого списку.
 * @param opts     Опції рендерингу.
 * @param item     Пункт списку з текстом і рівнем вкладеності.
 * @param y_offset [in,out] Поточний вертикальний зсув.
 * @param out      Контейнер шляхів для доповнення.
 * @return 0 при успіху; 2 при помилці рендерингу.
 */
static int markdown_render_ul_item (
    const markdown_opts_t *opts, const md_list_item_t *item, double *y_offset, geom_paths_t *out) {
    if (!opts || !item || !item->text || !y_offset || !out)
        return 2;

    const double bullet_r = 1.1;
    const double bullet_gutter = 2.0;
    const double indent_step = 6.0;

    md_inline_buffer_t inline_buf;
    md_inline_buffer_init (&inline_buf);
    if (md_inline_parse (item->text, &inline_buf) != 0) {
        md_inline_buffer_dispose (&inline_buf);
        return 2;
    }

    double size_pt = markdown_default_font_size (opts);
    double indent_mm = indent_step * (double)item->level;
    if (!(indent_mm >= 0.0))
        indent_mm = 0.0;
    double x_text_offset = indent_mm + (bullet_r * 2.0) + bullet_gutter;
    double frame_w = opts->frame_width_mm - x_text_offset;
    if (!(frame_w > 10.0))
        frame_w = 10.0;

    text_render_info_t info;
    md_render_block_t block;
    if (md_render_inline_block (
            opts, &inline_buf, size_pt, frame_w, x_text_offset, *y_offset, true, TEXT_ALIGN_LEFT,
            false, &info, &block)
        != 0) {
        md_inline_buffer_dispose (&inline_buf);
        return 2;
    }
    md_inline_buffer_dispose (&inline_buf);

    double line_height_mm = markdown_line_height (&info, size_pt);
    double top = 0.0, bottom = 0.0;
    markdown_first_line_bounds (
        &block.paths, block.lines, block.line_count, *y_offset, line_height_mm,
        line_height_mm * 0.2, &top, &bottom);
    double bullet_center_y = 0.5 * (top + bottom);

    if (paths_append (out, &block.paths) != 0) {
        md_render_block_dispose (&block);
        return 2;
    }

    shape_circle (out, indent_mm + bullet_r, bullet_center_y, bullet_r, 0.2);

    double block_h = block.bbox.max_y - block.bbox.min_y;
    if (!(block_h > 0.0))
        block_h = line_height_mm;
    *y_offset += block_h + markdown_block_gap (opts);

    md_render_block_dispose (&block);
    return 0;
}

/**
 * @brief Зчитує один пункт впорядкованого списку.
 * @param cursor       Поточна позиція у вихідному тексті.
 * @param item         [out] Опис пункту.
 * @param marker_value [out] Початкове числове значення маркера (може бути NULL).
 * @param out_next     [out] Початок наступного рядка (може бути NULL).
 * @return 1 при успіху; 0 якщо маркер відсутній; 2 при помилці памʼяті.
 */
static int markdown_read_ol_item (
    const char *cursor, md_list_item_t *item, int *marker_value, const char **out_next) {
    if (out_next)
        *out_next = cursor;
    if (marker_value)
        *marker_value = 0;
    if (!cursor || !item)
        return 0;

    const char *line_end = strchr (cursor, '\n');
    size_t line_len = line_end ? (size_t)(line_end - cursor) : strlen (cursor);
    if (line_len == 0 || markdown_is_blank_line (cursor, line_len))
        return 0;

    int indent = count_indent_spaces (cursor, line_len);
    const char *marker = cursor + indent;
    size_t digits = 0;
    if (!is_ol_marker (marker, line_len - (size_t)indent, &digits))
        return 0;

    int value = (int)strtol (marker, NULL, 10);
    if (marker_value)
        *marker_value = value;

    const char *content = marker + digits + 2; /* '.' і пробіл */
    size_t content_len = (size_t)(line_len - (content - cursor));
    char *text = (char *)malloc (content_len + 1);
    if (!text)
        return 2;
    memcpy (text, content, content_len);
    text[content_len] = '\0';

    md_list_item_dispose (item);
    item->text = text;
    item->level = indent / 2;
    if (item->level < 0)
        item->level = 0;
    if (item->level > 31)
        item->level = 31;
    item->next = line_end ? (line_end + 1) : (cursor + line_len);
    if (out_next)
        *out_next = item->next;
    return 1;
}

/**
 * @brief Додає до полотна один пункт впорядкованого списку.
 * @param opts     Опції рендерингу.
 * @param item     Пункт із текстом та рівнем вкладеності.
 * @param number   Порядковий номер, який слід відобразити.
 * @param y_offset [in,out] Поточний вертикальний зсув.
 * @param out      Контейнер шляхів для доповнення.
 * @return 0 при успіху; 2 при помилці рендерингу.
 */
static int markdown_render_ol_item (
    const markdown_opts_t *opts,
    const md_list_item_t *item,
    int number,
    double *y_offset,
    geom_paths_t *out) {
    if (!opts || !item || !item->text || !y_offset || !out)
        return 2;

    const double indent_step = 6.0;
    const double label_gutter = 2.0;

    double size_pt = markdown_default_font_size (opts);
    double indent_mm = indent_step * (double)item->level;
    if (!(indent_mm >= 0.0))
        indent_mm = 0.0;

    /* Лейбл (номер). */
    char label_text[32];
    snprintf (label_text, sizeof label_text, "%d.", number);

    md_inline_buffer_t label_buf;
    md_inline_buffer_init (&label_buf);
    if (md_inline_parse (label_text, &label_buf) != 0) {
        md_inline_buffer_dispose (&label_buf);
        return 2;
    }

    text_render_info_t label_info;
    md_render_block_t label_block;
    if (md_render_inline_block (
            opts, &label_buf, size_pt, opts->frame_width_mm, 0.0, 0.0, true, TEXT_ALIGN_LEFT, false,
            &label_info, &label_block)
        != 0) {
        md_inline_buffer_dispose (&label_buf);
        return 2;
    }
    md_inline_buffer_dispose (&label_buf);

    double label_width = label_block.bbox.max_x - label_block.bbox.min_x;
    if (!(label_width > 0.0))
        label_width = pt_to_mm (size_pt * 0.6);

    /* Текст пункту. */
    md_inline_buffer_t item_buf;
    md_inline_buffer_init (&item_buf);
    if (md_inline_parse (item->text, &item_buf) != 0) {
        md_inline_buffer_dispose (&item_buf);
        md_render_block_dispose (&label_block);
        return 2;
    }

    double x_text_offset = indent_mm + label_width + label_gutter;
    double frame_w = opts->frame_width_mm - x_text_offset;
    if (!(frame_w > 10.0))
        frame_w = 10.0;

    text_render_info_t item_info;
    md_render_block_t item_block;
    if (md_render_inline_block (
            opts, &item_buf, size_pt, frame_w, x_text_offset, *y_offset, true, TEXT_ALIGN_LEFT,
            false, &item_info, &item_block)
        != 0) {
        md_inline_buffer_dispose (&item_buf);
        md_render_block_dispose (&label_block);
        return 2;
    }
    md_inline_buffer_dispose (&item_buf);

    /* Вирівнювання базових ліній. */
    double text_baseline = (item_block.line_count > 0) ? item_block.lines[0].baseline_y : 0.0;
    double label_baseline = (label_block.line_count > 0) ? label_block.lines[0].baseline_y : 0.0;
    double label_y = *y_offset + text_baseline - label_baseline;

    geom_paths_t label_shifted;
    if (geom_paths_translate (&label_block.paths, indent_mm, label_y, &label_shifted) != 0) {
        md_render_block_dispose (&label_block);
        md_render_block_dispose (&item_block);
        return 2;
    }

    if (paths_append (out, &label_shifted) != 0) {
        geom_paths_free (&label_shifted);
        md_render_block_dispose (&label_block);
        md_render_block_dispose (&item_block);
        return 2;
    }
    geom_paths_free (&label_shifted);

    if (paths_append (out, &item_block.paths) != 0) {
        md_render_block_dispose (&label_block);
        md_render_block_dispose (&item_block);
        return 2;
    }

    /* Оновити вертикальний зсув. */
    double top = item_block.bbox.min_y;
    double bottom = item_block.bbox.max_y;
    double label_top = label_block.bbox.min_y + label_y;
    double label_bottom = label_block.bbox.max_y + label_y;
    if (label_top < top)
        top = label_top;
    if (label_bottom > bottom)
        bottom = label_bottom;

    double line_height_mm = markdown_line_height (&item_info, size_pt);
    double block_h = bottom - top;
    if (!(block_h > 0.0))
        block_h = line_height_mm;
    *y_offset += block_h + markdown_block_gap (opts);

    md_render_block_dispose (&label_block);
    md_render_block_dispose (&item_block);
    return 0;
}

/**
 * @brief Прагне відрендерити заголовок (рядок із префіксом '#').
 * @param p        Поточна позиція у тексті.
 * @param opts     Опції рендерингу.
 * @param y_offset [in,out] Поточний вертикальний зсув.
 * @param out      Контейнер шляхів.
 * @param info     Необовʼязково: статистика рендеру (може бути NULL).
 * @param out_next [out] Позиція після блоку (може бути NULL).
 * @return 1, якщо блок опрацьовано; 0, якщо це не заголовок; 2 при помилці.
 */
static int markdown_try_heading (
    const char *p,
    const markdown_opts_t *opts,
    double *y_offset,
    geom_paths_t *out,
    text_render_info_t *info,
    const char **out_next) {
    if (out_next)
        *out_next = p;
    if (!p)
        return 0;

    const char *line_end = strchr (p, '\n');
    size_t linelen = line_end ? (size_t)(line_end - p) : strlen (p);
    if (linelen == 0)
        return 0;

    int level = 0;
    const char *cursor = p;
    while ((size_t)(cursor - p) < linelen && *cursor == '#') {
        ++level;
        ++cursor;
    }
    if (level == 0 || level > 3 || cursor >= p + linelen || *cursor != ' ')
        return 0;

    ++cursor; /* пропустити пробіл після #... */
    size_t text_len = (size_t)(linelen - (cursor - p));
    char *heading = (char *)malloc (text_len + 1);
    if (!heading)
        return 2;
    memcpy (heading, cursor, text_len);
    heading[text_len] = '\0';

    double size_pt = (level == 1) ? 24.0 : (level == 2) ? 18.0 : 14.0;
    double block_h = 0.0;
    int rc = render_text_block (heading, opts, size_pt, *y_offset, out, info, &block_h);
    free (heading);
    if (rc != 0)
        return 2;

    if (!(block_h > 0.0))
        block_h = pt_to_mm (size_pt);
    *y_offset += block_h + markdown_block_gap (opts);
    if (out_next)
        *out_next = line_end ? (line_end + 1) : (p + linelen);
    return 1;
}

/**
 * @brief Прагне зібрати та відрендерити параграф (сукупність рядків).
 * @param p        Поточна позиція у тексті.
 * @param opts     Опції рендерингу.
 * @param y_offset [in,out] Поточний вертикальний зсув.
 * @param out      Контейнер шляхів.
 * @param info     Необовʼязково: статистика рендеру (може бути NULL).
 * @param out_next [out] Позиція після блоку (може бути NULL).
 * @return 1, якщо параграф опрацьовано; 0, якщо рядок порожній; 2 при помилці памʼяті.
 */
static int markdown_try_paragraph (
    const char *p,
    const markdown_opts_t *opts,
    double *y_offset,
    geom_paths_t *out,
    text_render_info_t *info,
    const char **out_next) {
    if (out_next)
        *out_next = p;
    if (!p)
        return 0;

    size_t cap = 0;
    size_t len = 0;
    char *buffer = NULL;
    const char *cursor = p;
    bool has_lines = false;

    while (*cursor) {
        const char *line_end = strchr (cursor, '\n');
        size_t line_len = line_end ? (size_t)(line_end - cursor) : strlen (cursor);
        if (line_len == 0 || markdown_is_blank_line (cursor, line_len))
            break;

        /* Якщо наступний рядок починає новий блочний елемент (UL/OL/TABLE),
         * завершуємо параграф тут і дозволяємо зовнішньому циклу
         * обробити список. */
        int indent = count_indent_spaces (cursor, line_len);
        const char *marker = cursor + indent;
        size_t rem = (line_len > (size_t)indent) ? (line_len - (size_t)indent) : 0;
        size_t digits = 0;
        if (rem >= 2 && is_ul_marker (*marker) && marker[1] == ' ')
            break;
        if (rem >= 3 && is_ol_marker (marker, rem, &digits))
            break;
        /* Перевірка на таблицю: поточний рядок містить '|', а наступний — роздільник. */
        if (memchr (cursor, '|', line_len)) {
            const char *nline = line_end ? (line_end + 1) : (cursor + line_len);
            if (*nline) {
                const char *nend = strchr (nline, '\n');
                size_t nlen = nend ? (size_t)(nend - nline) : strlen (nline);
                int tmp_align[128];
                size_t tmp_cols = 0;
                if (table_is_separator_line (nline, nlen, tmp_align, &tmp_cols))
                    break;
            }
        }

        size_t need = len + line_len + 2;
        if (need > cap) {
            size_t new_cap = cap ? cap * 2 : 256;
            while (new_cap < need)
                new_cap *= 2;
            char *grown = (char *)realloc (buffer, new_cap);
            if (!grown) {
                free (buffer);
                return 2;
            }
            buffer = grown;
            cap = new_cap;
        }
        memcpy (buffer + len, cursor, line_len);
        len += line_len;
        buffer[len++] = ' ';
        cursor = line_end ? (line_end + 1) : (cursor + line_len);
        has_lines = true;
    }

    if (!has_lines) {
        free (buffer);
        return 0;
    }

    if (len > 0)
        buffer[len - 1] = '\0';
    else
        buffer[0] = '\0';

    double size_pt = markdown_default_font_size (opts);
    double block_h = 0.0;
    int rc = render_text_block (buffer, opts, size_pt, *y_offset, out, info, &block_h);
    free (buffer);
    if (rc != 0)
        return 2;

    if (!(block_h > 0.0))
        block_h = pt_to_mm (size_pt);
    *y_offset += block_h + markdown_block_gap (opts);

    while (*cursor == '\n')
        ++cursor;
    if (out_next)
        *out_next = cursor;
    return 1;
}

/**
 * Відрендерити один текстовий блок заданим кеглем і змістити його на `y_offset_mm` вниз.
 * @param text         Текст блоку в UTF‑8 (не NULL).
 * @param opts         Опції рендерингу (родина, ширина кадру).
 * @param size_pt      Кегль у пунктах (pt).
 * @param y_offset_mm  Зміщення по Y в міліметрах.
 * @param accum        Призначення для додавання шляхів.
 * @param info_out     Необовʼязково: метадані рендерингу.
 * @return 0 при успіху; 1 при помилці.
 */
static int render_text_block (
    const char *text,
    const markdown_opts_t *opts,
    double size_pt,
    double y_offset_mm,
    geom_paths_t *accum,
    text_render_info_t *info_out,
    double *out_height_mm) {
    if (!text || !opts || !accum)
        return 1;

    md_inline_buffer_t inline_buf;
    md_inline_buffer_init (&inline_buf);
    if (md_inline_parse (text, &inline_buf) != 0) {
        md_inline_buffer_dispose (&inline_buf);
        return 1;
    }

    text_render_info_t fallback_info;
    text_render_info_t *info_ptr = info_out ? info_out : &fallback_info;

    md_render_block_t block;
    if (md_render_inline_block (
            opts, &inline_buf, size_pt, opts->frame_width_mm, 0.0, y_offset_mm, false,
            TEXT_ALIGN_LEFT, false, info_ptr, &block)
        != 0) {
        md_inline_buffer_dispose (&inline_buf);
        return 1;
    }

    int rc = paths_append (accum, &block.paths);
    if (rc == 0 && out_height_mm)
        *out_height_mm = block.bbox.max_y - block.bbox.min_y;

    md_render_block_dispose (&block);
    md_inline_buffer_dispose (&inline_buf);
    return rc;
}

/**
 * Порахувати кількість «пробільних» позицій відступу на початку рядка.
 * Табуляція рахується як 4 пробіли.
 * @param line   Початок рядка.
 * @param linelen Довжина рядка.
 * @return Кількість відступу у позиціях.
 */
static int count_indent_spaces (const char *line, size_t linelen) {
    int spaces = 0;
    for (size_t i = 0; i < linelen; ++i) {
        char c = line[i];
        if (c == ' ')
            spaces += 1;
        else if (c == '\t')
            spaces += 4;
        else
            break;
    }
    return spaces;
}

/**
 * Перевірити, чи символ є маркером невпорядкованого списку.
 * @param c Символ.
 * @return 1 якщо `c` ∈ {'-','*','+'}; інакше 0.
 */
static int is_ul_marker (char c) { return c == '-' || c == '*' || c == '+'; }

/**
 * Перевірити, чи рядок починається з маркера впорядкованого списку: DIGITS '.' пробіл.
 * @param line   Початок рядка.
 * @param len    Довжина рядка до кінця або до \n.
 * @param[out] digits_len Скільки цифр у префіксі (0, якщо не OL).
 * @return 1, якщо це елемент OL; інакше 0.
 */
static int is_ol_marker (const char *line, size_t len, size_t *digits_len) {
    size_t i = 0;
    while (i < len && isdigit ((unsigned char)line[i]))
        ++i;
    if (i == 0 || i + 1 >= len)
        return 0;
    if (line[i] == '.' && line[i + 1] == ' ') {
        if (digits_len)
            *digits_len = i;
        return 1;
    }
    return 0;
}

/**
 * Розібрати та відрендерити блок‑цитату (рядки, що починаються з '>').
 * @param p        Поточна позиція у тексті.
 * @param opts     Опції рендерингу.
 * @param y_offset [in,out] Поточний вертикальний зсув у мм.
 * @param out      Акумулятор шляхів (додаються результати).
 * @param p_out    [out] Позиція після блоку (або p, якщо це не цитата).
 * @return 1 якщо блок відрендерено; 0 якщо це не цитата; 2 при помилці памʼяті.
 */
static int parse_blockquote (
    const char *p,
    const markdown_opts_t *opts,
    double *y_offset,
    geom_paths_t *out,
    const char **p_out) {
    char *block_text = NULL;
    const char *next = p;
    int collect = markdown_collect_prefixed_lines (p, '>', &block_text, &next);
    if (collect == 0) {
        if (p_out)
            *p_out = p;
        return 0;
    }
    if (collect == 2) {
        free (block_text);
        return 2;
    }

    md_inline_buffer_t inline_buf;
    md_inline_buffer_init (&inline_buf);
    if (md_inline_parse (block_text, &inline_buf) != 0) {
        free (block_text);
        md_inline_buffer_dispose (&inline_buf);
        return 2;
    }
    free (block_text);

    const double bar_w_mm = 1.0;
    const double gutter_mm = 2.0;
    double size_pt = markdown_default_font_size (opts);
    double frame_w = opts->frame_width_mm - (bar_w_mm + gutter_mm);
    if (!(frame_w > 10.0))
        frame_w = 10.0;

    md_render_block_t block;
    if (md_render_inline_block (
            opts, &inline_buf, size_pt, frame_w, bar_w_mm + gutter_mm, *y_offset, false,
            TEXT_ALIGN_LEFT, false, NULL, &block)
        != 0) {
        md_inline_buffer_dispose (&inline_buf);
        return 2;
    }
    md_inline_buffer_dispose (&inline_buf);

    if (paths_append (out, &block.paths) != 0) {
        md_render_block_dispose (&block);
        return 2;
    }

    double y_top = block.bbox.min_y;
    double y_bot = block.bbox.max_y;
    if (!(y_bot > y_top)) {
        y_top = *y_offset;
        y_bot = *y_offset + pt_to_mm (size_pt);
    }
    geom_point_t bar[2] = { { 0.0, y_top }, { 0.0, y_bot } };
    shape_polyline (out, bar, 2, 0);

    double block_h = y_bot - y_top;
    if (!(block_h > 0.0))
        block_h = pt_to_mm (size_pt);
    *y_offset += block_h + markdown_block_gap (opts);

    md_render_block_dispose (&block);
    if (p_out)
        *p_out = next;
    return 1;
}

/**
 * Розібрати та відрендерити невпорядкований список (UL).
 * @param p        Поточна позиція у тексті.
 * @param opts     Опції рендерингу.
 * @param y_offset [in,out] Поточний вертикальний зсув у мм.
 * @param out      Акумулятор шляхів.
 * @param p_out    [out] Позиція після блоку (або p, якщо це не список).
 * @return 1, якщо список відрендерено; 0, якщо це не список; 2 — помилка памʼяті.
 */
static int parse_unordered_list (
    const char *p,
    const markdown_opts_t *opts,
    double *y_offset,
    geom_paths_t *out,
    const char **p_out) {
    const char *cursor = p;
    const char *next = cursor;
    md_list_item_t item = { 0 };

    int status = markdown_read_ul_item (cursor, &item, &next);
    if (status == 0) {
        if (p_out)
            *p_out = p;
        return 0;
    }
    if (status == 2)
        return 2;

    while (status == 1) {
        if (markdown_render_ul_item (opts, &item, y_offset, out) != 0) {
            md_list_item_dispose (&item);
            return 2;
        }
        md_list_item_dispose (&item);
        cursor = next;
        status = markdown_read_ul_item (cursor, &item, &next);
        if (status == 2) {
            md_list_item_dispose (&item);
            return 2;
        }
    }

    md_list_item_dispose (&item);
    if (p_out)
        *p_out = cursor;
    return 1;
}

/**
 * Розібрати та відрендерити впорядкований список (OL) з авто-нумерацією.
 *
 * Підтримує вкладеність через відступи: кожні 2 пробіли збільшують рівень.
 * Нумерація для кожного рівня ведеться окремо та починається з 1, якщо у
 * першому елементі рівня не задано інше стартове число (через префікс).
 *
 * @param p        Поточна позиція у тексті.
 * @param opts     Опції рендерингу.
 * @param y_offset [in,out] Поточний вертикальний зсув у мм.
 * @param out      Акумулятор шляхів.
 * @param p_out    [out] Позиція після блоку (або p, якщо це не список).
 * @return 1, якщо список відрендерено; 0, якщо це не список; 2 — помилка памʼяті.
 */
static int parse_ordered_list (
    const char *p,
    const markdown_opts_t *opts,
    double *y_offset,
    geom_paths_t *out,
    const char **p_out) {
    const char *cursor = p;
    const char *next = cursor;
    md_list_item_t item = { 0 };
    int marker_value = 0;

    int status = markdown_read_ol_item (cursor, &item, &marker_value, &next);
    if (status == 0) {
        if (p_out)
            *p_out = p;
        return 0;
    }
    if (status == 2)
        return 2;

    int counters[32] = { 0 };
    int prev_level = -1;

    while (status == 1) {
        int level = item.level;
        if (level < 0)
            level = 0;
        if (level > 31)
            level = 31;

        if (level > prev_level) {
            counters[level] = (marker_value > 0) ? marker_value - 1 : 0;
        } else if (level < prev_level) {
            for (int i = level + 1; i < 32; ++i)
                counters[i] = 0;
        }
        prev_level = level;
        counters[level]++;

        if (markdown_render_ol_item (opts, &item, counters[level], y_offset, out) != 0) {
            md_list_item_dispose (&item);
            return 2;
        }

        md_list_item_dispose (&item);
        cursor = next;
        status = markdown_read_ol_item (cursor, &item, &marker_value, &next);
        if (status == 2) {
            md_list_item_dispose (&item);
            return 2;
        }
    }

    md_list_item_dispose (&item);
    if (p_out)
        *p_out = cursor;
    return 1;
}

/**
 * @brief Розбирає Markdown‑документ і будує контурні шляхи.
 *
 * Алгоритм:
 *  1) Ітерація по рядках; пропускаємо початкові порожні рядки.
 *  2) Визначаємо тип блоку (заголовок, цитата, список UL або параграф).
 *  3) Рендеримо блок у тимчасові шляхи `geom_paths_t` (у мм) і додаємо у загальний
 *     контейнер, зсуваючи по `y_offset`.
 *  4) Збільшуємо `y_offset` на висоту блока + базовий міжрядковий відступ.
 *
 * @param text  Вхідний Markdown у UTF‑8 (не NULL).
 * @param opts  Опції рендерингу (родина, базовий кегль, ширина кадру).
 * @param out   Вихідні шляхи (ініціалізуються всередині; викликач звільняє `geom_paths_free`).
 * @param info  Необовʼязково: загальні метадані рендеру тексту.
 * @return 0 при успіху; 1 при помилці памʼяті/параметрів.
 */
int markdown_render_paths (
    const char *text, const markdown_opts_t *opts, geom_paths_t *out, text_render_info_t *info) {
    if (!text || !opts || !out)
        return 1;
    if (geom_paths_init (out, GEOM_UNITS_MM) != 0)
        return 1;

    const char *cursor = text;
    double y_offset = 0.0;

    while (*cursor) {
        while (*cursor == '\n' || *cursor == '\r')
            ++cursor;
        if (!*cursor)
            break;

        const char *next = cursor;
        int handled = markdown_try_heading (cursor, opts, &y_offset, out, info, &next);
        if (handled == 2) {
            geom_paths_free (out);
            return 1;
        }
        if (handled == 1) {
            cursor = next;
            continue;
        }

        handled = parse_blockquote (cursor, opts, &y_offset, out, &next);
        if (handled == 2) {
            geom_paths_free (out);
            return 1;
        }
        if (handled == 1) {
            cursor = next;
            continue;
        }

        handled = parse_ordered_list (cursor, opts, &y_offset, out, &next);
        if (handled == 2) {
            geom_paths_free (out);
            return 1;
        }
        if (handled == 1) {
            cursor = next;
            continue;
        }

        handled = parse_unordered_list (cursor, opts, &y_offset, out, &next);
        if (handled == 2) {
            geom_paths_free (out);
            return 1;
        }
        if (handled == 1) {
            cursor = next;
            continue;
        }

        handled = markdown_try_table (cursor, opts, &y_offset, out, &next);
        if (handled == 2) {
            geom_paths_free (out);
            return 1;
        }
        if (handled == 1) {
            cursor = next;
            continue;
        }

        handled = markdown_try_paragraph (cursor, opts, &y_offset, out, info, &next);
        if (handled == 2) {
            geom_paths_free (out);
            return 1;
        }
        if (handled == 1) {
            cursor = next;
            continue;
        }

        /* Якщо нічого не спрацювало — перейти до наступного рядка, щоб уникнути циклу. */
        const char *fallback = strchr (cursor, '\n');
        cursor = fallback ? (fallback + 1) : (cursor + strlen (cursor));
    }

    return 0;
}
