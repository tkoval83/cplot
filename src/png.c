/**
 * @file png.c
 * @brief Реалізація растрового превʼю (PNG).
 * @ingroup png
 * @details
 * Генерує мінімальне PNG (IHDR/IDAT/IEND) у відтінках сірого (8‑біт) з
 * некорпімованим Deflate‑потоком (zlib контейнер). Лінії конвертуються з
 * міліметрових координат у пікселі за сталим DPI і растеризуються алгоритмом
 * Брезенгема.
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

/** \brief Базова роздільність превʼю у точках на дюйм. */
#define PNG_DPI 96.0

/**
 * @brief Запис 32‑бітного значення у форматі big‑endian.
 * @param dst Вказівник на 4‑байтовий буфер призначення.
 * @param value Значення для запису.
 */
static void png_write_u32_be (uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)((value >> 24) & 0xFF);
    dst[1] = (uint8_t)((value >> 16) & 0xFF);
    dst[2] = (uint8_t)((value >> 8) & 0xFF);
    dst[3] = (uint8_t)(value & 0xFF);
}

/**
 * @brief Обчислює CRC‑32 (поліном 0xEDB88320) для шматка даних.
 * @param crc Поточне значення CRC (0 — для початку). Внутрішньо застосовується XOR із 0xFFFFFFFF.
 * @param data Дані.
 * @param len Довжина у байтах.
 * @return Оновлене CRC‑32.
 */
static uint32_t png_crc32_update (uint32_t crc, const uint8_t *data, size_t len) {
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
 * @brief Оновлює контрольну суму Adler‑32 для zlib.
 * @param adler Поточне значення (типово 1).
 * @param data Дані.
 * @param len Довжина.
 * @return Нова Adler‑32.
 */
static uint32_t png_adler32_update (uint32_t adler, const uint8_t *data, size_t len) {
    uint32_t s1 = adler & 0xFFFFU;
    uint32_t s2 = (adler >> 16) & 0xFFFFU;
    for (size_t i = 0; i < len; ++i) {
        s1 = (s1 + data[i]) % 65521U;
        s2 = (s2 + s1) % 65521U;
    }
    return (s2 << 16) | s1;
}

/**
 * @brief Додає байти у динамічний буфер із автоматичним розширенням.
 * @param buf [in,out] Вказівник на буфер (може бути `NULL` на старті).
 * @param len [in,out] Поточна довжина буфера.
 * @param cap [in,out] Ємність буфера.
 * @param data Джерело байтів.
 * @param add Кількість байтів для додавання.
 * @return 0 — успіх; -1 — помилка виділення памʼяті.
 */
static int png_append_bytes (uint8_t **buf, size_t *len, size_t *cap, const uint8_t *data, size_t add) {
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
 * @brief Додає один байт у динамічний буфер.
 */
static int png_append_byte (uint8_t **buf, size_t *len, size_t *cap, uint8_t value) {
    return png_append_bytes (buf, len, cap, &value, 1);
}

/**
 * @brief Додає PNG‑чанк у буфер: length + type + data + CRC.
 * @param buf [in,out] Буфер PNG.
 * @param len [in,out] Поточна довжина.
 * @param cap [in,out] Ємність.
 * @param type 4‑символьний тип ("IHDR", "IDAT", ...).
 * @param data Тіло чанка (може бути `NULL` при `data_len==0`).
 * @param data_len Довжина даних.
 * @return 0 — успіх; -1 — помилка памʼяті.
 */
static int png_append_chunk (
    uint8_t **buf,
    size_t *len,
    size_t *cap,
    const char type[4],
    const uint8_t *data,
    uint32_t data_len) {
    uint8_t header[8];
    png_write_u32_be (header, data_len);
    memcpy (header + 4, type, 4);
    if (png_append_bytes (buf, len, cap, header, 8) != 0)
        return -1;
    if (data_len > 0 && png_append_bytes (buf, len, cap, data, data_len) != 0)
        return -1;
    uint32_t crc = png_crc32_update (0, (const uint8_t *)type, 4);
    crc = png_crc32_update (crc, data, data_len);
    uint8_t crc_bytes[4];
    png_write_u32_be (crc_bytes, crc);
    return png_append_bytes (buf, len, cap, crc_bytes, 4);
}

/**
 * @brief Рисує відрізок на растровому полотні чорним кольором (0).
 * @param pixels Буфер пікселів у форматі 8‑біт сірого, рядок за рядком.
 * @param width Ширина в пікселях.
 * @param height Висота в пікселях.
 * @param scale Множник перетворення мм→піксель.
 * @param x0_mm,y0_mm Початкова точка у мм.
 * @param x1_mm,y1_mm Кінцева точка у мм.
 */
static void png_draw_line (
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
 * @copydoc png_render_layout
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
            png_draw_line (
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
        raw[y * row_bytes] = 0;
        memcpy (&raw[y * row_bytes + 1], &pixels[y * width_px], width_px);
    }
    free (pixels);

    uint8_t *png = NULL;
    size_t len = 0;
    size_t cap = 0;

    const uint8_t signature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    if (png_append_bytes (&png, &len, &cap, signature, sizeof (signature)) != 0)
        goto fail;

    uint8_t ihdr[13];
    png_write_u32_be (&ihdr[0], (uint32_t)width_px);
    png_write_u32_be (&ihdr[4], (uint32_t)height_px);
    ihdr[8] = 8;
    ihdr[9] = 0;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    if (png_append_chunk (&png, &len, &cap, "IHDR", ihdr, sizeof (ihdr)) != 0)
        goto fail;

    size_t zcap = raw_size + raw_size / 255 + 100;
    uint8_t *zdata = (uint8_t *)malloc (zcap);
    if (!zdata)
        goto fail;
    size_t zlen = 0;

    png_append_byte (&zdata, &zlen, &zcap, 0x78);
    png_append_byte (&zdata, &zlen, &zcap, 0x01);

    size_t offset = 0;
    uint32_t adler = 1;
    while (offset < raw_size) {
        size_t chunk = raw_size - offset;
        if (chunk > 65535)
            chunk = 65535;
        int final = (offset + chunk == raw_size) ? 1 : 0;
        png_append_byte (&zdata, &zlen, &zcap, (uint8_t)(final));
        uint16_t len16 = (uint16_t)chunk;
        uint16_t nlen = ~len16;
        png_append_byte (&zdata, &zlen, &zcap, len16 & 0xFF);
        png_append_byte (&zdata, &zlen, &zcap, (len16 >> 8) & 0xFF);
        png_append_byte (&zdata, &zlen, &zcap, nlen & 0xFF);
        png_append_byte (&zdata, &zlen, &zcap, (nlen >> 8) & 0xFF);
        if (png_append_bytes (&zdata, &zlen, &zcap, raw + offset, chunk) != 0) {
            free (zdata);
            goto fail;
        }
        adler = png_adler32_update (adler, raw + offset, chunk);
        offset += chunk;
    }

    uint8_t adler_bytes[4];
    png_write_u32_be (adler_bytes, adler);
    png_append_bytes (&zdata, &zlen, &zcap, adler_bytes, sizeof (adler_bytes));
    free (raw);

    if (png_append_chunk (&png, &len, &cap, "IDAT", zdata, (uint32_t)zlen) != 0) {
        free (zdata);
        goto fail;
    }
    free (zdata);

    if (png_append_chunk (&png, &len, &cap, "IEND", NULL, 0) != 0)
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
