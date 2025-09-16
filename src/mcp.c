/**
 * @file mcp.c
 * @brief Простий MCP‑сервер (JSON‑RPC 2.0 через stdio, NDJSON).
 *
 * Реалізує сервер, що читає запити JSON‑RPC 2.0 з stdin (по одному JSON на рядок)
 * та друкує відповіді у stdout. Призначено для інтеграції із агентами через MCP.
 *
 * Підтримувані методи:
 * - ping → { pong: true }
 * - list_tools → перелік інструментів/можливостей
 * - call_tool → виклик інструменту (початково: print із режимом preview)
 *
 * Обмеження:
 * - Парсинг JSON виконано мінімалістично через утиліти json.c; це не повний валідатор.
 * - На даному етапі call_tool підтримує лише інструмент "print" у режимі прев’ю.
 */

#include "mcp.h"

#include "cmd.h"
#include "json.h"
#include "log.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* JSON-утиліти винесено до json.c/json.h */

/* ---- Base64 -------------------------------------------------------------- */

/**
 * Таблиця кодування Base64.
 */
static const char b64tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * Закодувати буфер у Base64.
 *
 * @param data    Вхідні байти.
 * @param len     Довжина вхідних байтів.
 * @param out_len Вихід: довжина текстового представлення (без NUL); може бути NULL.
 * @return Новий NUL‑термінований рядок або NULL при нестачі пам’яті (звільняє викликач).
 */
static char *base64_encode (const uint8_t *data, size_t len, size_t *out_len) {
    size_t olen = 4 * ((len + 2) / 3);
    char *out = (char *)malloc (olen + 1);
    if (!out)
        return NULL;
    size_t i = 0, j = 0;
    while (i < len) {
        uint32_t octet_a = i < len ? data[i++] : 0;
        uint32_t octet_b = i < len ? data[i++] : 0;
        uint32_t octet_c = i < len ? data[i++] : 0;
        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;
        out[j++] = b64tab[(triple >> 18) & 0x3F];
        out[j++] = b64tab[(triple >> 12) & 0x3F];
        out[j++] = (i - 1 > len) ? '=' : b64tab[(triple >> 6) & 0x3F];
        out[j++] = (i > len) ? '=' : b64tab[triple & 0x3F];
    }
    /* Підправити '=' залежно від залишку */
    size_t mod = len % 3;
    if (mod) {
        out[olen - 1] = '=';
        if (mod == 1)
            out[olen - 2] = '=';
    }
    out[olen] = '\0';
    if (out_len)
        *out_len = olen;
    return out;
}

/* ---- Побудова відповідей ------------------------------------------------ */

typedef void (*json_emit_fn) (json_writer_t *w, void *ctx);

/**
 * Надіслати успішну відповідь JSON‑RPC.
 *
 * @param id_ptr  Вказівник на сирий фрагмент значення поля id (може бути числом або рядком у
 * лапках).
 * @param id_len  Довжина фрагмента id.
 * @param payload Сирий JSON для поля result (має бути валідним JSON‑фрагментом).
 */
static void write_json_ok (const char *id_ptr, size_t id_len, json_emit_fn emit, void *ctx) {
    json_writer_t w;
    jsonw_init (&w, stdout);
    jsonw_begin_object (&w);
    jsonw_key (&w, "jsonrpc");
    jsonw_string_cstr (&w, "2.0");
    jsonw_key (&w, "id");
    if (!id_ptr)
        jsonw_null (&w);
    else
        jsonw_raw (&w, id_ptr, id_len);
    jsonw_key (&w, "result");
    if (emit)
        emit (&w, ctx);
    else
        jsonw_null (&w);
    jsonw_end_object (&w);
    fputc ('\n', stdout);
    fflush (stdout);
}

/**
 * Надіслати помилкову відповідь JSON‑RPC.
 *
 * @param id_ptr  Вказівник на сирий фрагмент id або NULL (для id:null).
 * @param id_len  Довжина фрагмента id.
 * @param code    Код помилки.
 * @param message Повідомлення українською (буде екрановано як рядок у JSON).
 */
static void write_json_err (const char *id_ptr, size_t id_len, int code, const char *message) {
    json_writer_t w;
    jsonw_init (&w, stdout);
    jsonw_begin_object (&w);
    jsonw_key (&w, "jsonrpc");
    jsonw_string_cstr (&w, "2.0");
    jsonw_key (&w, "id");
    if (!id_ptr)
        jsonw_null (&w);
    else
        jsonw_raw (&w, id_ptr, id_len);
    jsonw_key (&w, "error");
    jsonw_begin_object (&w);
    jsonw_key (&w, "code");
    jsonw_int (&w, code);
    jsonw_key (&w, "message");
    jsonw_string_cstr (&w, message);
    jsonw_end_object (&w);
    jsonw_end_object (&w);
    fputc ('\n', stdout);
    fflush (stdout);
}

/* ---- Обробники методів -------------------------------------------------- */

/**
 * Обробити метод ping.
 * @param id_ptr Сирий id.
 * @param id_len Довжина id.
 */
static void emit_ping_result (json_writer_t *w, void *ctx) {
    (void)ctx;
    jsonw_begin_object (w);
    jsonw_key (w, "pong");
    jsonw_bool (w, 1);
    jsonw_end_object (w);
}

static void handle_ping (const char *id_ptr, size_t id_len) {
    write_json_ok (id_ptr, id_len, emit_ping_result, NULL);
}

/**
 * Обробити метод list_tools — повернути перелік інструментів MCP.
 * @param id_ptr Сирий id.
 * @param id_len Довжина id.
 */
static void emit_list_tools_result (json_writer_t *w, void *ctx) {
    (void)ctx;
    jsonw_begin_object (w);
    jsonw_key (w, "tools");
    jsonw_begin_array (w);
    const struct {
        const char *name;
        const char *desc;
    } tools[] = {
        { "print", "Попередній перегляд або друк розкладки" },
        { "device", "Операції з пристроєм" },
        { "fonts", "Перелік шрифтів" },
        { "config", "Налаштування" },
        { "version", "Версія застосунку" },
        { "sysinfo", "Діагностика" },
    };
    for (size_t i = 0; i < sizeof tools / sizeof tools[0]; ++i) {
        jsonw_begin_object (w);
        jsonw_key (w, "name");
        jsonw_string_cstr (w, tools[i].name);
        jsonw_key (w, "description");
        jsonw_string_cstr (w, tools[i].desc);
        jsonw_end_object (w);
    }
    jsonw_end_array (w);
    jsonw_end_object (w);
}

/* Контекст та емітер для prev'ю print */
struct preview_ctx {
    const char *fmt;
    const char *b64;
    size_t b64len;
};

static void emit_preview_result (json_writer_t *w, void *vctx) {
    struct preview_ctx *c = (struct preview_ctx *)vctx;
    jsonw_begin_object (w);
    jsonw_key (w, "ok");
    jsonw_bool (w, 1);
    jsonw_key (w, "fmt");
    jsonw_string_cstr (w, c->fmt);
    jsonw_key (w, "preview_b64");
    jsonw_string (w, c->b64, c->b64len);
    jsonw_end_object (w);
}

static void handle_list_tools (const char *id_ptr, size_t id_len) {
    write_json_ok (id_ptr, id_len, emit_list_tools_result, NULL);
}

/**
 * Виконати інструмент print (у режимі прев’ю) з параметрів запиту.
 *
 * Підтримувані поля у запиті: preview (bool), png (bool), orientation ("portrait"|"landscape"),
 * input (рядок). Інші параметри наразі мають типові значення.
 *
 * @param id_ptr Сирий id запиту.
 * @param id_len Довжина id.
 * @param json   Повний сирий JSON рядок запиту (використовується для вибіркового читання полів).
 */
static void handle_call_tool_print (const char *id_ptr, size_t id_len, const char *json) {
    /* Параметри: args.preview, args.png, args.orientation, margins/paper/family/verbose/dry_run,
     * input */
    /* Спрощення: підтримуємо лише preview=true; інші — типові значення */
    int preview = json_get_bool (json, "preview", 0);
    int png = json_get_bool (json, "png", 0);
    char *orient = json_get_string (json, "orientation", NULL);
    int orientation = 1; /* portrait */
    if (orient && strcmp (orient, "landscape") == 0)
        orientation = 2;
    free (orient);

    size_t in_len = 0;
    char *in_str = json_get_string (json, "input", &in_len);
    string_t in_view = { .chars = in_str ? in_str : "",
                         .len = in_len ? in_len : (in_str ? strlen (in_str) : 0),
                         .enc = STR_ENC_UTF8 };

    if (!preview) {
        free (in_str);
        write_json_err (id_ptr, id_len, -32602, "Режим друку без прев’ю наразі недоступний у MCP");
        return;
    }

    bytes_t out = { 0 };
    int rc = cmd_print_preview_execute (
        in_view, /* font */ NULL,
        /* paper */ 160.0, 101.0,
        /* margins */ 10.0, 10.0, 10.0, 10.0, orientation, png ? PREVIEW_FMT_PNG : PREVIEW_FMT_SVG,
        VERBOSE_OFF, &out);

    if (rc != 0) {
        free (in_str);
        write_json_err (id_ptr, id_len, -32000, "Не вдалося згенерувати прев’ю");
        return;
    }

    size_t b64len = 0;
    char *b64 = base64_encode (out.bytes, out.len, &b64len);
    free (out.bytes);
    free (in_str);
    if (!b64) {
        write_json_err (id_ptr, id_len, -32001, "Недостатньо пам’яті для кодування base64");
        return;
    }
    struct preview_ctx ctx = { .fmt = png ? "png" : "svg", .b64 = b64, .b64len = b64len };
    write_json_ok (id_ptr, id_len, emit_preview_result, &ctx);
    free (b64);
}

/**
 * Обробити метод call_tool: роутінг за полем name.
 *
 * @param id_ptr Сирий id.
 * @param id_len Довжина id.
 * @param json   Повний JSON запит.
 */
static void handle_call_tool (const char *id_ptr, size_t id_len, const char *json) {
    char *name = json_get_string (json, "name", NULL);
    if (!name) {
        write_json_err (id_ptr, id_len, -32602, "Відсутня назва інструменту");
        return;
    }
    if (strcmp (name, "print") == 0) {
        free (name);
        handle_call_tool_print (id_ptr, id_len, json);
        return;
    }
    free (name);
    write_json_err (id_ptr, id_len, -32601, "Інструмент не підтримується у цій версії MCP");
}

/* ---- Основний цикл ------------------------------------------------------ */

/**
 * Головний цикл MCP‑сервера.
 *
 * Зчитує рядки зі stdin, виконує мінімальний розбір JSON та викликає відповідні
 * обробники. Кожен рядок повинен бути завершеним JSON‑документом (NDJSON).
 *
 * @return 0 — нормальне завершення (EOF), 1 — помилка вводу/виводу (не використовується наразі).
 */
int mcp_server_run (void) {
    LOGI ("Запуск MCP‑сервера (stdio, NDJSON)");
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    while ((n = getline (&line, &cap, stdin)) != -1) {
        /* Зняти кінцеві пробіли */
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        if (n == 0)
            continue;

        const char *id_ptr = NULL;
        size_t id_len = 0;
        json_get_raw (line, "id", &id_ptr, &id_len);

        char *method = json_get_string (line, "method", NULL);
        if (!method) {
            write_json_err (id_ptr, id_len, -32600, "Некоректний запит (відсутній method)");
            continue;
        }
        if (strcmp (method, "ping") == 0) {
            handle_ping (id_ptr, id_len);
        } else if (strcmp (method, "list_tools") == 0) {
            handle_list_tools (id_ptr, id_len);
        } else if (strcmp (method, "call_tool") == 0) {
            handle_call_tool (id_ptr, id_len, line);
        } else {
            write_json_err (id_ptr, id_len, -32601, "Невідомий метод");
        }
        free (method);
    }
    free (line);
    LOGI ("MCP‑сервер завершено (EOF)");
    return 0;
}
