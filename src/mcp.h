/**
 * @file mcp.h
 * @brief MCP сервер (JSON-RPC 2.0 через stdio, NDJSON).
 *
 * Сервер реалізує мінімальний протокол JSON‑RPC 2.0 для взаємодії з агентами.
 * Транспорт — стандартні потоки: stdin для запитів і stdout для відповідей.
 * Формат — NDJSON: по одному JSON‑документу на рядок (UTF‑8).
 *
 * Підтримувані методи:
 * - ping → { "pong": true }
 * - list_tools → опис доступних інструментів (назва, короткий опис)
 * - call_tool → виклик інструменту за ім’ям; початково підтримується "print"
 *
 * Вхідні/вихідні структури (скорочено):
 * - Запит: { "jsonrpc":"2.0", "id":<число|рядок>, "method":"...", "params":{...} }
 * - Успіх: { "jsonrpc":"2.0", "id":<як у запиті>, "result":{...} }
 * - Помилка: { "jsonrpc":"2.0", "id":<як у запиті або null>, "error":{ "code":<int>,
 * "message":<рядок> } }
 *
 * Зауваги:
 * - Парсер JSON — спрощений (див. json.h), очікує коректні вхідні дані.
 * - call_tool/print наразі підтримує лише прев’ю та повертає байти у Base64 з полем fmt.
 */
#ifndef CPLOT_MCP_H
#define CPLOT_MCP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Запустити MCP‑сервер поверх stdin/stdout.
 *
 * Контракт:
 * - Приймає по одному JSON на рядок (NDJSON), кодування UTF‑8.
 * - Підтримує методи: ping, list_tools, call_tool.
 * - Відповіді друкуються у stdout як один JSON на рядок.
 * - Повернення з функції відбувається лише у разі EOF на stdin або фатальної помилки.
 *
 * @return 0 — нормальне завершення; 1 — помилка вводу/виводу.
 */
int mcp_server_run (void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CPLOT_MCP_H */
