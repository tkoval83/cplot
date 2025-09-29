/**
 * @file serial.h
 * @brief Платформо-незалежний інтерфейс роботи з послідовним портом.
 * @defgroup serial Serial
 * @ingroup device
 * @details
 * Надає мінімальний набір операцій для відкриття/закриття, читання/запису та
 * простих службових дій над POSIX‑сумісним серійним портом. Використовує
 * неблокуючі дескриптори і `poll(2)` для тайм‑аутів. Усі повідомлення —
 * українською. Порт відкривається у режимі raw без керування потоком.
 */
#ifndef SERIAL_H
#define SERIAL_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Непрозорий дескриптор відкритого серійного порту.
 */
typedef struct serial_port_s serial_port_t;

/**
 * @brief Відкриває серійний порт.
 * @param path Шлях до пристрою (наприклад, `/dev/tty.usbmodem*`).
 * @param baud Швидкість у бодах (9600, 115200, ...).
 * @param read_timeout_ms Тайм‑аут читання за замовчуванням (мс; >0).
 * @param errbuf [out] Якщо не `NULL` — буфер для тексту помилки.
 * @param errlen Розмір `errbuf` у байтах.
 * @return Дескриптор `serial_port_t*` або `NULL` у разі помилки (з повідомленням у `errbuf`).
 */
serial_port_t *
serial_open (const char *path, int baud, int read_timeout_ms, char *errbuf, size_t errlen);

/**
 * @brief Закриває серійний порт та вивільняє ресурси.
 * @param sp Порт; `NULL` — no‑op.
 */
void serial_close (serial_port_t *sp);

/**
 * @brief Пише дані у порт з тайм‑аутом.
 * @param sp Порт.
 * @param data Дані для запису.
 * @param len Довжина даних у байтах.
 * @param timeout_ms Тайм‑аут очікування готовності на запис (мс; >0).
 * @return Кількість записаних байтів (може бути < `len` при тайм‑ауті) або -1 при помилці.
 */
ssize_t serial_write (serial_port_t *sp, const void *data, size_t len, int timeout_ms);

/**
 * @brief Зчитує дані з порту з тайм‑аутом.
 * @param sp Порт.
 * @param buf [out] Буфер призначення.
 * @param len Розмір буфера.
 * @param timeout_ms Тайм‑аут (мс; якщо <=0 — використовується значення з `serial_open`).
 * @return Кількість прочитаних байтів (0 при тайм‑ауті) або -1 при помилці.
 */
ssize_t serial_read (serial_port_t *sp, void *buf, size_t len, int timeout_ms);

/**
 * @brief Очищає вхідний буфер порту, дочитуючи наявні байти.
 * @param sp Порт.
 * @return Кількість відкинутих байтів (>=0) або -1 при помилці.
 */
int serial_flush_input (serial_port_t *sp);

/**
 * @brief Надсилає рядок та CR (\r) наприкінці.
 * @param sp Порт.
 * @param s Рядок ASCII.
 * @return 0 — успіх; -1 — помилка.
 */
int serial_write_line (serial_port_t *sp, const char *s);

/**
 * @brief Зчитує рядок до CR/LF або тайм‑ауту.
 * @param sp Порт.
 * @param buf [out] Буфер для рядка (термінатор `\0` додається, якщо дозволяє розмір).
 * @param maxlen Розмір буфера.
 * @param timeout_ms Тайм‑аут (мс).
 * @return Довжина рядка без термінатора; 0 — тайм‑аут; -1 — помилка.
 */
ssize_t serial_read_line (serial_port_t *sp, char *buf, size_t maxlen, int timeout_ms);

/**
 * @brief Перевіряє наявність EBB (EggBot Board) — надсилає "V" і очікує відповідь.
 * @param sp Порт.
 * @param version_out [out] Буфер для рядка версії (може бути `NULL`).
 * @param version_len Розмір буфера версії.
 * @return 0 — успіх; -1 — помилка або тайм‑аут.
 */
int serial_probe_ebb (serial_port_t *sp, char *version_out, size_t version_len);

#ifdef __APPLE__

/**
 * @brief Евристичний пошук шляху до порту AxiDraw (macOS).
 * @param out_path [out] Буфер для шляху (наприклад, `/dev/tty.usbmodemXXXX`).
 * @param out_len Розмір буфера.
 * @return 0 — знайдено; -1 — не знайдено або помилка.
 */
int serial_guess_axidraw_port (char *out_path, size_t out_len);
#endif

#ifdef __cplusplus
}
#endif

#endif
