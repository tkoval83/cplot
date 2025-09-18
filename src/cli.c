/**
 * @file cli.c
 * @brief Minimal subcommand dispatcher.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
 * Прочитати файл повністю у пам'ять.
 *
 * @param path Шлях до файлу (не NULL).
 * @param out  Буфер для результату; після успіху out->bytes звільняє викликач.
 * @return 0 при успіху; -1 при помилці (errno містить деталі).
 */
static int read_file_bytes (const char *path, bytes_t *out) {
    if (!path || !out)
        return -1;
    out->bytes = NULL;
    out->len = 0;

    FILE *fp = fopen (path, "rb");
    if (!fp)
        return -1;

    if (fseek (fp, 0, SEEK_END) != 0) {
        fclose (fp);
        return -1;
    }
    long size = ftell (fp);
    if (size < 0) {
        fclose (fp);
        return -1;
    }
    if (fseek (fp, 0, SEEK_SET) != 0) {
        fclose (fp);
        return -1;
    }

    size_t len = (size_t)size;
    uint8_t *buf = (uint8_t *)malloc (len + 1);
    if (!buf) {
        fclose (fp);
        errno = ENOMEM;
        return -1;
    }

    size_t rd = fread (buf, 1, len, fp);
    if (rd != len) {
        if (ferror (fp)) {
            int saved = errno;
            fclose (fp);
            free (buf);
            errno = saved ? saved : EIO;
            return -1;
        }
    }
    buf[rd] = '\0';
    fclose (fp);

    out->bytes = buf;
    out->len = rd;
    return 0;
}

/**
 * Обробити інтерактивний режим (CMD_NONE).
 */
static int dispatch_interactive (const options_t *options, verbose_level_t verbose) {
    LOGI ("Запуск інтерактивного режиму AxiDraw");
    trace_write (
        LOG_INFO, "CLI interactive: порт=%s модель=%s",
        options->device_port[0] ? options->device_port : "<авто>",
        options->device_model[0] ? options->device_model : "<типова>");
    const char *port = options->device_port[0] ? options->device_port : NULL;
    const char *model = options->device_model[0] ? options->device_model : NULL;
    return cmd_device_shell (port, model, verbose);
}

/**
 * Обробити підкоманду version.
 */
static int dispatch_version (verbose_level_t verbose) {
    trace_write (LOG_INFO, "CLI version: показати версію");
    return cmd_version_execute (verbose);
}

/**
 * Обробити підкоманду fonts.
 */
static int dispatch_fonts (verbose_level_t verbose) {
    trace_write (LOG_INFO, "CLI fonts: перелік шрифтів");
    return cmd_fonts_execute (verbose);
}

/**
 * Обробити підкоманду config.
 */
static int dispatch_config (const options_t *options, verbose_level_t verbose) {
    config_t cfg; /* TODO: load real config */
    const char *model = options->device_model[0] ? options->device_model : CONFIG_DEFAULT_MODEL;
    config_factory_defaults (&cfg, model);
    const char *pairs = options->config_set_pairs[0] ? options->config_set_pairs : NULL;
    trace_write (
        LOG_INFO, "CLI config: action=%s model=%s pairs=%s",
        config_action_name (options->config_action), model, pairs ? pairs : "<none>");
    return cmd_config_execute (options->config_action, pairs, &cfg, verbose);
}

/**
 * Обробити підкоманду device.
 */
static int dispatch_device (const options_t *options, verbose_level_t verbose) {
    trace_write (
        LOG_INFO, "CLI device: action=%s pen=%s motor=%s порт=%s модель=%s jog=(%.3f,%.3f)",
        device_action_kind_name (options->device_action.kind),
        device_pen_action_name (options->device_action.pen),
        device_motor_action_name (options->device_action.motor),
        options->device_port[0] ? options->device_port : "<авто>",
        options->device_model[0] ? options->device_model : "<типова>", options->jog_dx_mm,
        options->jog_dy_mm);
    const char *port = options->device_port[0] ? options->device_port : NULL;
    const char *model = options->device_model[0] ? options->device_model : NULL;
    return cmd_device_execute (
        &options->device_action, port, model, options->jog_dx_mm, options->jog_dy_mm, verbose);
}

/**
 * Обробити підкоманду print.
 */
static int dispatch_print (const options_t *options, verbose_level_t verbose) {
    bool has_file = options->file_name[0] != '\0';
    bool has_text = options->input_text[0] != '\0';

    if (has_file && has_text) {
        LOGE ("Вкажіть лише одне джерело даних: файл або --text.");
        return 2;
    }
    if (!has_file && !has_text) {
        LOGE ("Не вказано джерело даних. Передайте шлях до файлу або використайте --text.");
        return 2;
    }

    bytes_t file_buf = { 0 };
    string_t input;
    const char *source_desc = has_text ? "текст" : "файл";

    if (has_text) {
        input.chars = options->input_text;
        input.len = strlen (options->input_text);
    } else {
        if (read_file_bytes (options->file_name, &file_buf) != 0) {
            LOGE ("Не вдалося прочитати файл \"%s\": %s", options->file_name, strerror (errno));
            return 1;
        }
        input.chars = (const char *)file_buf.bytes;
        input.len = file_buf.len;
    }
    input.enc = STR_ENC_UTF8;

    trace_write (
        LOG_INFO,
        "CLI print: джерело=%s preview=%s формат=%s dry=%s орієнтація=%s папір=%.2fx%.2f "
        "поля=(%.2f,%.2f,%.2f,%.2f)",
        source_desc, options->preview ? "так" : "ні",
        options->preview ? (options->preview_png ? "png" : "svg") : "n/a",
        options->dry_run ? "так" : "ні", orientation_name (options->orientation),
        options->paper_w_mm, options->paper_h_mm, options->margin_top_mm, options->margin_right_mm,
        options->margin_bottom_mm, options->margin_left_mm);

    int rc;
    if (options->preview) {
        bytes_t preview = { 0 };
        rc = cmd_print_preview_execute (
            input, options->font_family[0] ? options->font_family : NULL, options->paper_w_mm,
            options->paper_h_mm, options->margin_top_mm, options->margin_right_mm,
            options->margin_bottom_mm, options->margin_left_mm, options->orientation,
            options->preview_png ? PREVIEW_FMT_PNG : PREVIEW_FMT_SVG, verbose, &preview);
        if (rc == 0 && preview.bytes && preview.len > 0)
            fwrite (preview.bytes, 1, preview.len, stdout);
        free (preview.bytes);
    } else {
        rc = cmd_print_execute (
            input, options->font_family[0] ? options->font_family : NULL, options->paper_w_mm,
            options->paper_h_mm, options->margin_top_mm, options->margin_right_mm,
            options->margin_bottom_mm, options->margin_left_mm, options->orientation,
            options->dry_run, verbose);
    }

    free (file_buf.bytes);
    return rc;
}

/**
 * Обробити підкоманду sysinfo.
 */
static int dispatch_sysinfo (verbose_level_t verbose) {
    trace_write (LOG_INFO, "CLI sysinfo: запит діагностики");
    return cmd_sysinfo_execute (verbose);
}

/**
 * Обробити невідому або неочікувану підкоманду.
 */
static int dispatch_unknown (void) {
    LOGW ("невідома підкоманда — показ довідки");
    trace_write (LOG_WARN, "CLI: невідома підкоманда");
    usage ();
    return 2;
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
    trace_write (LOG_INFO, "CLI: підкоманда %s", cmd_name (options->cmd));

    switch (options->cmd) {
    case CMD_NONE:
        return dispatch_interactive (options, verbose);
    case CMD_VERSION:
        return dispatch_version (verbose);
    case CMD_FONTS:
        return dispatch_fonts (verbose);
    case CMD_CONFIG:
        return dispatch_config (options, verbose);
    case CMD_DEVICE:
        return dispatch_device (options, verbose);
    case CMD_PRINT:
        return dispatch_print (options, verbose);
    case CMD_SYSINFO:
        return dispatch_sysinfo (verbose);
    default:
        return dispatch_unknown ();
    }
}
