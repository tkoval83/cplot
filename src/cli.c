/**
 * @file cli.c
 * @brief Minimal subcommand dispatcher.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "args.h"
#include "cli.h"
#include "cmd.h"
#include "config.h"
#include "help.h"
#include "log.h"

/**
 * Викликати реалізацію відповідної підкоманди.
 *
 * На поточному етапі підкоманди є заглушками й лише виводять повідомлення.
 *
 * @param options Розібрані опції командного рядка (не NULL).
 * @return Код завершення процесу: 0 — успіх, 2 — показати usage, 1 — внутрішня помилка.
 */
int cli_dispatch (const options_t *options) {
    if (!options) {
        LOGE ("внутрішня помилка: options == NULL");
        return 1;
    }

    verbose_level_t verbose = options->verbose ? VERBOSE_ON : VERBOSE_OFF;

    switch (options->cmd) {
    case CMD_NONE:
        LOGW ("не вказано підкоманду — показ довідки");
        usage ();
        return 2;
    case CMD_VERSION:
        return cmd_version_execute (verbose);
    case CMD_FONTS:
        return cmd_fonts_execute (verbose);
    case CMD_CONFIG: {
        config_t cfg; /* TODO: load real config */
        config_factory_defaults (&cfg);
        const char *pairs = options->config_set_pairs[0] ? options->config_set_pairs : NULL;
        return cmd_config_execute (options->config_action, pairs, &cfg, verbose);
    }
    case CMD_DEVICE:
        return cmd_device_execute (
            options->device_action, options->device_port[0] ? options->device_port : NULL,
            options->device_model[0] ? options->device_model : NULL, options->jog_dx_mm,
            options->jog_dy_mm, verbose);
    case CMD_PRINT: {
        /* Зчитати вхідний файл або stdin у пам'ять */
        uint8_t *buf = NULL;
        size_t len = 0;
        FILE *fp = NULL;
        if (options->file_name[0] == '-' && options->file_name[1] == '\0') {
            fp = stdin;
        } else {
            fp = fopen (options->file_name, "rb");
            if (!fp) {
                LOGE ("Не вдалося відкрити вхідний файл");
                return 1;
            }
        }
        /* прочитати весь вміст */
        if (fp == stdin) {
            /* читання зі stdin у розширюваний буфер */
            size_t cap = 64 * 1024;
            buf = (uint8_t *)malloc (cap);
            if (!buf) {
                if (fp != stdin)
                    fclose (fp);
                LOGE ("Недостатньо пам'яті для читання даних");
                return 1;
            }
            size_t n;
            while ((n = fread (buf + len, 1, cap - len, fp)) > 0) {
                len += n;
                if (len == cap) {
                    cap *= 2;
                    uint8_t *nb = (uint8_t *)realloc (buf, cap);
                    if (!nb) {
                        free (buf);
                        if (fp != stdin)
                            fclose (fp);
                        LOGE ("Недостатньо пам'яті для читання даних");
                        return 1;
                    }
                    buf = nb;
                }
            }
        } else {
            if (fseek (fp, 0, SEEK_END) != 0) {
                fclose (fp);
                LOGE ("Не вдалося визначити розмір файлу");
                return 1;
            }
            long sz = ftell (fp);
            if (sz < 0) {
                fclose (fp);
                LOGE ("Не вдалося визначити розмір файлу");
                return 1;
            }
            rewind (fp);
            buf = (uint8_t *)malloc ((size_t)sz);
            if (!buf) {
                fclose (fp);
                LOGE ("Недостатньо пам'яті для читання файлу");
                return 1;
            }
            len = fread (buf, 1, (size_t)sz, fp);
            fclose (fp);
        }

        int rc;
        string_t input = { .chars = (const char *)buf, .len = len, .enc = STR_ENC_UTF8 };
        if (options->preview) {
            bytes_t out;
            rc = cmd_print_preview (
                input, options->font_family[0] ? options->font_family : NULL, options->paper_w_mm,
                options->paper_h_mm, options->margin_top_mm, options->margin_right_mm,
                options->margin_bottom_mm, options->margin_left_mm, options->orientation,
                options->preview_png ? PREVIEW_FMT_PNG : PREVIEW_FMT_SVG, verbose, &out);
            if (rc == 0 && out.bytes && out.len > 0) {
                fwrite (out.bytes, 1, out.len, stdout);
            }
            free (out.bytes);
        } else {
            rc = cmd_print_execute (
                input, options->font_family[0] ? options->font_family : NULL, options->paper_w_mm,
                options->paper_h_mm, options->margin_top_mm, options->margin_right_mm,
                options->margin_bottom_mm, options->margin_left_mm, options->orientation,
                options->dry_run, verbose);
        }
        free (buf);
        return rc;
    }
    case CMD_SYSINFO:
        return cmd_sysinfo_execute (verbose);
    default:
        LOGW ("невідома підкоманда — показ довідки");
        usage ();
        return 2;
    }
}
