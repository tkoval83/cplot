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
#include "trace.h"

static const char *cmd_name (cmd_t cmd) {
    switch (cmd) {
    case CMD_NONE:
        return "interactive";
    case CMD_PRINT:
        return "print";
    case CMD_DEVICE:
        return "device";
    case CMD_FONTS:
        return "fonts";
    case CMD_CONFIG:
        return "config";
    case CMD_VERSION:
        return "version";
    case CMD_SYSINFO:
        return "sysinfo";
    default:
        return "unknown";
    }
}

static const char *config_action_name (int action) {
    switch (action) {
    case CFG_SHOW:
        return "show";
    case CFG_RESET:
        return "reset";
    case CFG_SET:
        return "set";
    default:
        return "none";
    }
}

static const char *device_action_kind_name (device_action_kind_t kind) {
    switch (kind) {
    case DEVICE_ACTION_NONE:
        return "none";
    case DEVICE_ACTION_LIST:
        return "list";
    case DEVICE_ACTION_SHELL:
        return "shell";
    case DEVICE_ACTION_PEN:
        return "pen";
    case DEVICE_ACTION_MOTORS:
        return "motors";
    case DEVICE_ACTION_ABORT:
        return "abort";
    case DEVICE_ACTION_HOME:
        return "home";
    case DEVICE_ACTION_JOG:
        return "jog";
    case DEVICE_ACTION_VERSION:
        return "version";
    case DEVICE_ACTION_STATUS:
        return "status";
    case DEVICE_ACTION_POSITION:
        return "position";
    case DEVICE_ACTION_RESET:
        return "reset";
    case DEVICE_ACTION_REBOOT:
        return "reboot";
    default:
        return "unknown";
    }
}

static const char *device_pen_action_name (device_pen_action_t action) {
    switch (action) {
    case DEVICE_PEN_NONE:
        return "none";
    case DEVICE_PEN_UP:
        return "up";
    case DEVICE_PEN_DOWN:
        return "down";
    case DEVICE_PEN_TOGGLE:
        return "toggle";
    default:
        return "unknown";
    }
}

static const char *device_motor_action_name (device_motor_action_t action) {
    switch (action) {
    case DEVICE_MOTOR_NONE:
        return "none";
    case DEVICE_MOTOR_ON:
        return "on";
    case DEVICE_MOTOR_OFF:
        return "off";
    default:
        return "unknown";
    }
}

static const char *orientation_name (orientation_t orient) {
    switch (orient) {
    case ORIENT_PORTRAIT:
        return "portrait";
    case ORIENT_LANDSCAPE:
        return "landscape";
    default:
        return "unknown";
    }
}

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
    trace_write (
        LOG_INFO, "CLI: підкоманда %s", cmd_name (options->cmd));

    switch (options->cmd) {
    case CMD_NONE:
        LOGI ("Запуск інтерактивного режиму AxiDraw");
        trace_write (
            LOG_INFO,
            "CLI interactive: порт=%s модель=%s",
            options->device_port[0] ? options->device_port : "<авто>",
            options->device_model[0] ? options->device_model : "<типова>");
        return cmd_device_shell (
            options->device_port[0] ? options->device_port : NULL,
            options->device_model[0] ? options->device_model : NULL, verbose);
    case CMD_VERSION:
        trace_write (LOG_INFO, "CLI version: показати версію");
        return cmd_version_execute (verbose);
    case CMD_FONTS:
        trace_write (LOG_INFO, "CLI fonts: перелік шрифтів");
        return cmd_fonts_execute (verbose);
    case CMD_CONFIG: {
        config_t cfg; /* TODO: load real config */
        const char *model = options->device_model[0] ? options->device_model : CONFIG_DEFAULT_MODEL;
        config_factory_defaults (&cfg, model);
        const char *pairs = options->config_set_pairs[0] ? options->config_set_pairs : NULL;
        trace_write (
            LOG_INFO,
            "CLI config: action=%s model=%s pairs=%s",
            config_action_name (options->config_action), model, pairs ? pairs : "<none>");
        return cmd_config_execute (options->config_action, pairs, &cfg, verbose);
    }
    case CMD_DEVICE:
        trace_write (
            LOG_INFO,
            "CLI device: action=%s pen=%s motor=%s port=%s модель=%s jog=(%.3f,%.3f)",
            device_action_kind_name (options->device_action.kind),
            device_pen_action_name (options->device_action.pen),
            device_motor_action_name (options->device_action.motor),
            options->device_port[0] ? options->device_port : "<авто>",
            options->device_model[0] ? options->device_model : "<типова>",
            options->jog_dx_mm,
            options->jog_dy_mm);
        return cmd_device_execute (
            &options->device_action, options->device_port[0] ? options->device_port : NULL,
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
        trace_write (
            LOG_INFO,
            "CLI print: preview=%s формат=%s dry=%s орієнтація=%s папір=%.2fx%.2f поля=(%.2f,%.2f,%.2f,%.2f)",
            options->preview ? "так" : "ні",
            options->preview ? (options->preview_png ? "png" : "svg") : "n/a",
            options->dry_run ? "так" : "ні",
            orientation_name (options->orientation),
            options->paper_w_mm,
            options->paper_h_mm,
            options->margin_top_mm,
            options->margin_right_mm,
            options->margin_bottom_mm,
            options->margin_left_mm);
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
        trace_write (LOG_INFO, "CLI sysinfo: запит діагностики");
        return cmd_sysinfo_execute (verbose);
    default:
        LOGW ("невідома підкоманда — показ довідки");
        trace_write (LOG_WARN, "CLI: невідома підкоманда");
        usage ();
        return 2;
    }
}
