/**
 * @file serial.h
 * @brief POSIX‑послідовний порт для зв’язку з EBB/AxiDraw.
 *
 * Надає мінімальний API для відкриття, читання/запису з тайм‑аутами
 * та допоміжні функції рядкового обміну з контролером EBB.
 *
 * Політика мови: усі коментарі та повідомлення українською.
 */
#ifndef SERIAL_H
#define SERIAL_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Дескриптор послідовного порту.
 */
typedef struct serial_port_s serial_port_t;

/**
 * Відкрити послідовний порт і налаштувати 8N1 без керування потоком.
 *
 * @param path         Шлях до пристрою (наприклад, "/dev/tty.usbmodem*").
 * @param baud         Швидкість (напр., 9600, 115200).
 * @param read_timeout_ms Типовий тайм‑аут читання (мс) для рядкових/простих операцій.
 * @param errbuf       Буфер для тексту помилки (може бути NULL).
 * @param errlen       Розмір буфера помилки (0, якщо errbuf == NULL).
 * @return Вказівник на serial_port_t або NULL у разі помилки.
 */
serial_port_t *
serial_open (const char *path, int baud, int read_timeout_ms, char *errbuf, size_t errlen);

/** Закрити порт і звільнити ресурс. */
void serial_close (serial_port_t *sp);

/**
 * Записати байти у порт із тайм‑аутом.
 * @param sp           Відкритий порт.
 * @param data         Дані для надсилання.
 * @param len          Довжина даних.
 * @param timeout_ms   Тайм‑аут (мс) на завершення запису (усі байти).
 * @return Кількість записаних байтів; -1 при помилці; 0 при тайм‑ауті без запису.
 */
ssize_t serial_write (serial_port_t *sp, const void *data, size_t len, int timeout_ms);

/**
 * Прочитати до len байтів із тайм‑аутом.
 * @param sp           Відкритий порт.
 * @param buf          Буфер призначення.
 * @param len          Максимальна кількість байтів.
 * @param timeout_ms   Загальний тайм‑аут (мс).
 * @return Кількість прочитаних байтів (>0), 0 при тайм‑ауті, -1 при помилці.
 */
ssize_t serial_read (serial_port_t *sp, void *buf, size_t len, int timeout_ms);

/** Скинути вхідний буфер (очистити все, що прийшло). */
int serial_flush_input (serial_port_t *sp);

/**
 * Відправити рядок та завершаючий CR (\r).
 * @param sp           Відкритий порт.
 * @param s            Нуль‑термінований рядок без CR/LF.
 * @return 0 при успіху; -1 при помилці/тайм‑ауті.
 */
int serial_write_line (serial_port_t *sp, const char *s);

/**
 * Прочитати до символу кінця рядка (\r або \n). Кінцевий символ у буфер НЕ включається.
 * @param sp           Відкритий порт.
 * @param buf          Буфер призначення.
 * @param maxlen       Максимальна довжина (включно з нуль‑термінатором).
 * @param timeout_ms   Загальний тайм‑аут (мс).
 * @return Довжина рядка (без термінатора) при успіху; 0 при тайм‑ауті; -1 при помилці.
 */
ssize_t serial_read_line (serial_port_t *sp, char *buf, size_t maxlen, int timeout_ms);

/**
 * Перевірка зв’язку з EBB: надіслати команду версії "V" та прочитати відповідь.
 * @param sp           Відкритий порт.
 * @param version_out  Буфер для відповіді (може бути NULL).
 * @param version_len  Розмір буфера (0, якщо version_out == NULL).
 * @return 0 при успіху (отримано рядок відповіді); -1 при помилці/тайм‑ауті.
 */
int serial_probe_ebb (serial_port_t *sp, char *version_out, size_t version_len);

#ifdef __APPLE__
/**
 * Спробувати знайти типовий порт AxiDraw на macOS (tty.usbmodem*).
 * @param out_path Буфер для шляху.
 * @param out_len  Довжина буфера.
 * @return 0 якщо знайдений і записаний у out_path; -1 якщо не знайдено.
 */
int serial_guess_axidraw_port (char *out_path, size_t out_len);
#endif

#ifdef __cplusplus
}
#endif

#endif /* SERIAL_H */
