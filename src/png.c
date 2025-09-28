/**
 * @file png.c
 * @brief Генерація PNG-превʼю з розкладки.
 */

#include "png.h"

#include "log.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif

#define PNG_DPI 96.0

/**
 * @brief Записати 32-бітне слово у big-endian.
 *
 * @param dst   Вихідний буфер (мінімум 4 байти).
 * @param value Значення для запису.
 */
static void write_u32_be (uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)((value >> 24) & 0xFF);
    dst[1] = (uint8_t)((value >> 16) & 0xFF);
    dst[2] = (uint8_t)((value >> 8) & 0xFF);
    dst[3] = (uint8_t)(value & 0xFF);
}

/**
 * @brief Оновити CRC-32 з використанням полінома PNG.
 *
 * @param crc  Поточне значення CRC (0 для початку).
 * @param data Дані для хешування.
 * @param len  Кількість байтів у даних.
 * @return Оновлене значення CRC.
 */
static uint32_t crc32_update (uint32_t crc, const uint8_t *data, size_t len) {
    static uint32_t table[256];
    static int table_init = 0;
    if (!table_init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int j = 0; j < 8; ++j)
                c = (c & 1) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        table_init = 1;
    }
    crc = crc ^ 0xFFFFFFFFU;
    for (size_t i = 0; i < len; ++i)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFU;
}

/**
 * @brief Оновити контрольну суму Adler-32 (zlib).
 *
 * @param adler Поточне значення (1 для початку).
 * @param data  Дані для хешування.
 * @param len   Кількість байтів.
 * @return Оновлене значення Adler-32.
 */
static uint32_t adler32_update (uint32_t adler, const uint8_t *data, size_t len) {
    uint32_t s1 = adler & 0xFFFFU;
    uint32_t s2 = (adler >> 16) & 0xFFFFU;
    for (size_t i = 0; i < len; ++i) {
        s1 = (s1 + data[i]) % 65521U;
        s2 = (s2 + s1) % 65521U;
    }
    return (s2 << 16) | s1;
}

/**
 * @brief Додати байти до динамічного буфера PNG.
 *
 * @param buf  Вказівник на буфер (realloc за потреби).
 * @param len  Поточна довжина буфера (оновлюється).
 * @param cap  Поточна місткість (оновлюється).
 * @param data Дані для додавання.
 * @param add  Кількість байтів.
 * @return 0 при успіху; -1 якщо не вдалося перевиділити памʼять.
 */
static int append_bytes (uint8_t **buf, size_t *len, size_t *cap, const uint8_t *data, size_t add) {
    size_t need = *len + add;
    if (need > *cap) {
        size_t new_cap = (*cap == 0) ? 1024 : *cap;
        while (new_cap < need)
            new_cap *= 2;
        uint8_t *grown = (uint8_t *)realloc (*buf, new_cap);
        if (!grown)
            return -1;
        *buf = grown;
        *cap = new_cap;
    }
    memcpy (*buf + *len, data, add);
    *len += add;
    return 0;
}

/**
 * @brief Додати один байт до буфера (обгортка над append_bytes).
 *
 * @param buf  Буфер.
 * @param len  Поточна довжина (оновлюється).
 * @param cap  Місткість (оновлюється).
 * @param value Байтове значення.
 * @return 0 при успіху; -1 при помилці памʼяті.
 */
static int append_byte (uint8_t **buf, size_t *len, size_t *cap, uint8_t value) {
    return append_bytes (buf, len, cap, &value, 1);
}

/**
 * @brief Додати оброблений PNG-чанк (довжина + тип + CRC).
 *
 * @param buf      Буфер PNG-файла.
 * @param len      Поточна довжина (оновлюється).
 * @param cap      Місткість (оновлюється).
 * @param type     4-символьний тип чанку.
 * @param data     Дані чанку (може бути NULL, якщо data_len == 0).
 * @param data_len Кількість байтів у даних.
 * @return 0 при успіху; -1 при помилці памʼяті.
 */
static int append_chunk (
    uint8_t **buf,
    size_t *len,
    size_t *cap,
    const char type[4],
    const uint8_t *data,
    uint32_t data_len) {
    uint8_t header[8];
    write_u32_be (header, data_len);
    memcpy (header + 4, type, 4);
    if (append_bytes (buf, len, cap, header, 8) != 0)
        return -1;
    if (data_len > 0 && append_bytes (buf, len, cap, data, data_len) != 0)
        return -1;
    uint32_t crc = crc32_update (0, (const uint8_t *)type, 4);
    crc = crc32_update (crc, data, data_len);
    uint8_t crc_bytes[4];
    write_u32_be (crc_bytes, crc);
    return append_bytes (buf, len, cap, crc_bytes, 4);
}

/**
 * @brief Намалювати відрізок Bresenhamʼом у растровому буфері (1 біт/піксель).
 *
 * @param pixels Буфер пікселів у відтінках сірого (0 — чорний).
 * @param width  Ширина растра, px.
 * @param height Висота растра, px.
 * @param scale  Коефіцієнт перетворення мм→px.
 * @param x0_mm  Початкова X у мм.
 * @param y0_mm  Початкова Y у мм.
 * @param x1_mm  Кінцева X у мм.
 * @param y1_mm  Кінцева Y у мм.
 */
static void draw_line (
    uint8_t *pixels,
    int width,
    int height,
    double scale,
    double x0_mm,
    double y0_mm,
    double x1_mm,
    double y1_mm) {
    double x0 = x0_mm * scale;
    double y0 = y0_mm * scale;
    double x1 = x1_mm * scale;
    double y1 = y1_mm * scale;
    int ix0 = (int)round (x0);
    int iy0 = (int)round (y0);
    int ix1 = (int)round (x1);
    int iy1 = (int)round (y1);

    int dx = abs (ix1 - ix0);
    int sx = ix0 < ix1 ? 1 : -1;
    int dy = -abs (iy1 - iy0);
    int sy = iy0 < iy1 ? 1 : -1;
    int err = dx + dy;

    while (1) {
        if (ix0 >= 0 && ix0 < width && iy0 >= 0 && iy0 < height)
            pixels[iy0 * width + ix0] = 0;
        if (ix0 == ix1 && iy0 == iy1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            ix0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            iy0 += sy;
        }
    }
}

/**
 * @brief Згенерувати монохромний PNG-файл для заданої розкладки.
 *
 * @param layout Готова розкладка (в мм).
 * @param[out] out Буфер PNG (malloc, звільняє викликач).
 * @return 0 при успіху; 1 при помилці памʼяті чи некоректних параметрах.
 */
int png_render_layout (const drawing_layout_t *layout, bytes_t *out) {
    if (!layout || !out)
        return 1;

    const canvas_layout_t *c = &layout->layout;
    double scale = PNG_DPI / 25.4;
    int width_px = (int)ceil (c->paper_w_mm * scale);
    int height_px = (int)ceil (c->paper_h_mm * scale);
    if (width_px <= 0 || height_px <= 0)
        return 1;

    uint8_t *pixels = (uint8_t *)malloc ((size_t)width_px * (size_t)height_px);
    if (!pixels)
        return 1;
    memset (pixels, 0xFF, (size_t)width_px * (size_t)height_px);

    const geom_paths_t *paths = &c->paths_mm;
    for (size_t i = 0; i < paths->len; ++i) {
        const geom_path_t *path = &paths->items[i];
        if (path->len < 2)
            continue;
        for (size_t j = 1; j < path->len; ++j)
            draw_line (
                pixels, width_px, height_px, scale, path->pts[j - 1].x, path->pts[j - 1].y,
                path->pts[j].x, path->pts[j].y);
    }

    size_t row_bytes = 1 + (size_t)width_px;
    size_t raw_size = row_bytes * (size_t)height_px;
    uint8_t *raw = (uint8_t *)malloc (raw_size);
    if (!raw) {
        free (pixels);
        return 1;
    }

    for (int y = 0; y < height_px; ++y) {
        raw[y * row_bytes] = 0; /* filter type none */
        memcpy (&raw[y * row_bytes + 1], &pixels[y * width_px], width_px);
    }
    free (pixels);

    uint8_t *png = NULL;
    size_t len = 0;
    size_t cap = 0;

    const uint8_t signature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    if (append_bytes (&png, &len, &cap, signature, sizeof (signature)) != 0)
        goto fail;

    uint8_t ihdr[13];
    write_u32_be (&ihdr[0], (uint32_t)width_px);
    write_u32_be (&ihdr[4], (uint32_t)height_px);
    ihdr[8] = 8;  /* bit depth */
    ihdr[9] = 0;  /* grayscale */
    ihdr[10] = 0; /* compression */
    ihdr[11] = 0; /* filter */
    ihdr[12] = 0; /* interlace */
    if (append_chunk (&png, &len, &cap, "IHDR", ihdr, sizeof (ihdr)) != 0)
        goto fail;

    size_t zcap = raw_size + raw_size / 255 + 100;
    uint8_t *zdata = (uint8_t *)malloc (zcap);
    if (!zdata)
        goto fail;
    size_t zlen = 0;

    /* zlib header: CMF/FLG */
    append_byte (&zdata, &zlen, &zcap, 0x78);
    append_byte (&zdata, &zlen, &zcap, 0x01);

    size_t offset = 0;
    uint32_t adler = 1;
    while (offset < raw_size) {
        size_t chunk = raw_size - offset;
        if (chunk > 65535)
            chunk = 65535;
        int final = (offset + chunk == raw_size) ? 1 : 0;
        append_byte (&zdata, &zlen, &zcap, (uint8_t)(final)); /* BTYPE=00 */
        uint16_t len16 = (uint16_t)chunk;
        uint16_t nlen = ~len16;
        append_byte (&zdata, &zlen, &zcap, len16 & 0xFF);
        append_byte (&zdata, &zlen, &zcap, (len16 >> 8) & 0xFF);
        append_byte (&zdata, &zlen, &zcap, nlen & 0xFF);
        append_byte (&zdata, &zlen, &zcap, (nlen >> 8) & 0xFF);
        if (append_bytes (&zdata, &zlen, &zcap, raw + offset, chunk) != 0) {
            free (zdata);
            goto fail;
        }
        adler = adler32_update (adler, raw + offset, chunk);
        offset += chunk;
    }

    uint8_t adler_bytes[4];
    write_u32_be (adler_bytes, adler);
    append_bytes (&zdata, &zlen, &zcap, adler_bytes, sizeof (adler_bytes));
    free (raw);

    if (append_chunk (&png, &len, &cap, "IDAT", zdata, (uint32_t)zlen) != 0) {
        free (zdata);
        goto fail;
    }
    free (zdata);

    if (append_chunk (&png, &len, &cap, "IEND", NULL, 0) != 0)
        goto fail;

    out->bytes = png;
    out->len = len;
    return 0;

fail:
    free (raw);
    free (png);
    LOGE ("Не вдалося сформувати прев’ю");
    return 1;
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
