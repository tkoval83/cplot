/**
 * @file cli.c
 * @brief Minimal subcommand dispatcher.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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
        /* Приймаємо вхідні дані ТІЛЬКИ зі stdin (конвеєр/редирект). */
        if (options->file_name[0]
            && !(options->file_name[0] == '-' && options->file_name[1] == '\0')) {
            LOGE (
                "Вхідні дані приймаються лише через stdin (конвеєр або редирект). Читання з "
                "файлу не підтримується.");
            return 2;
        }
        if (isatty (fileno (stdin))) {
            LOGE (
                "Немає даних у stdin. Передайте дані через конвеєр або редирект (наприклад: echo "
                "'...' | cplot print --preview -)");
            return 2;
        }

        /* Зчитати stdin у пам'ять (розширюваний буфер) */
        uint8_t *buf = NULL;
        size_t len = 0;
        size_t cap = 64 * 1024;
        buf = (uint8_t *)malloc (cap);
        if (!buf) {
            LOGE ("Недостатньо пам'яті для читання даних");
            return 1;
        }
        size_t n;
        while ((n = fread (buf + len, 1, cap - len, stdin)) > 0) {
            len += n;
            if (len == cap) {
                cap *= 2;
                uint8_t *nb = (uint8_t *)realloc (buf, cap);
                if (!nb) {
                    free (buf);
                    LOGE ("Недостатньо пам'яті для читання даних");
                    return 1;
                }
                buf = nb;
            }
        }

        int rc;
        string_t input = { .chars = (const char *)buf, .len = len, .enc = STR_ENC_UTF8 };
        if (options->preview) {
            bytes_t out;
            rc = cmd_print_preview_execute (
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
