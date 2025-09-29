/**
 * @file cli.c
 * @brief Реалізація маршрутизації CLI-підкоманд.
 * @ingroup cli
 */

#include "cli.h"

#include "args.h"
#include "cmd.h"
#include "help.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/**
 * @brief Маршрутизує виконання підкоманд згідно з розібраними опціями.
 * @param options Розібрані параметри CLI.
 * @param argc Початковий argc процесу (не використовується тут).
 * @param argv Початковий argv процесу (не використовується тут).
 * @return 0 — успіх, інакше код помилки підкоманди.
 */
int cli_run (const options_t *options, int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    if (!options)
        return 1;
    if (options->cmd == CMD_NONE) {
        cli_usage ();
        return 1;
    }

    cmd_set_output (NULL);
    switch (options->cmd) {
    case CMD_PRINT: {

        char *owned = NULL;
        size_t in_len = 0;
        const char *in_chars = NULL;
        if (options->file_name[0]) {
            FILE *fp = fopen (options->file_name, "rb");
            if (!fp)
                return 1;
            if (fseek (fp, 0, SEEK_END) != 0) {
                fclose (fp);
                return 1;
            }
            long sz = ftell (fp);
            if (sz < 0) {
                fclose (fp);
                return 1;
            }
            if (fseek (fp, 0, SEEK_SET) != 0) {
                fclose (fp);
                return 1;
            }
            owned = (char *)malloc ((size_t)sz + 1);
            if (!owned) {
                fclose (fp);
                return 1;
            }
            size_t rd = (sz > 0) ? fread (owned, 1, (size_t)sz, fp) : 0;
            fclose (fp);
            if (rd != (size_t)sz) {
                free (owned);
                return 1;
            }
            owned[rd] = '\0';
            in_chars = owned;
            in_len = rd;
        } else {
            if (isatty (STDIN_FILENO))
                return 1;
            size_t cap = 8192;
            size_t len = 0;
            owned = (char *)malloc (cap);
            if (!owned)
                return 1;
            while (!feof (stdin) && !ferror (stdin)) {
                if (len + 4096 > cap) {
                    size_t nc = cap * 2;
                    char *nb = (char *)realloc (owned, nc);
                    if (!nb) {
                        free (owned);
                        return 1;
                    }
                    owned = nb;
                    cap = nc;
                }
                size_t chunk = cap - len;
                size_t n = fread (owned + len, 1, chunk, stdin);
                len += n;
                if (n == 0)
                    break;
            }
            if (ferror (stdin)) {
                free (owned);
                return 1;
            }
            owned = (char *)realloc (owned, len + 1);
            if (!owned)
                return 1;
            owned[len] = '\0';
            in_chars = owned;
            in_len = len;
        }

        const char *family = options->font_family;
        const char *model = options->device_model;
        if (options->preview) {
            LOGD ("cli: fit_page option=%d", options->fit_page ? 1 : 0);
            uint8_t *bytes = NULL;
            size_t blen = 0;
            int rc = cmd_print_preview (
                in_chars, in_len, options->input_format == INPUT_FORMAT_MARKDOWN, family,
                options->font_size_pt, model, options->paper_w_mm, options->paper_h_mm,
                options->margin_top_mm, options->margin_right_mm, options->margin_bottom_mm,
                options->margin_left_mm, options->orientation, options->fit_page ? 1 : 0,
                options->preview_png ? 1 : 0, options->verbose, &bytes, &blen);
            if (rc == 0 && bytes) {
                if (options->output_path[0]) {
                    FILE *fp = fopen (options->output_path, "wb");
                    if (!fp)
                        rc = 1;
                    else {
                        fwrite (bytes, 1, blen, fp);
                        fclose (fp);
                    }
                } else {
                    fwrite (bytes, 1, blen, stdout);
                }
                free (bytes);
            }
            free (owned);
            return rc;
        } else {
            int rc = cmd_print_execute (
                in_chars, in_len, options->input_format == INPUT_FORMAT_MARKDOWN, family,
                options->font_size_pt, model, options->paper_w_mm, options->paper_h_mm,
                options->margin_top_mm, options->margin_right_mm, options->margin_bottom_mm,
                options->margin_left_mm, options->orientation, options->fit_page, options->dry_run,
                options->verbose);
            free (owned);
            return rc;
        }
    }
    case CMD_DEVICE: {
        const char *alias = options->remote_device;
        const char *model = options->device_model;
        switch (options->device_action.kind) {
        case DEVICE_ACTION_LIST:
            return cmd_device_list (model, options->verbose);
        case DEVICE_ACTION_PROFILE:
            return cmd_device_profile (alias, model, options->verbose);
        case DEVICE_ACTION_PEN:
            switch (options->device_action.pen) {
            case DEVICE_PEN_UP:
                return cmd_device_pen_up (alias, model, options->verbose);
            case DEVICE_PEN_DOWN:
                return cmd_device_pen_down (alias, model, options->verbose);
            case DEVICE_PEN_TOGGLE:
                return cmd_device_pen_toggle (alias, model, options->verbose);
            default:
                return 2;
            }
        case DEVICE_ACTION_MOTORS:
            return (options->device_action.motor == DEVICE_MOTOR_ON)
                       ? cmd_device_motors_on (alias, model, options->verbose)
                       : cmd_device_motors_off (alias, model, options->verbose);
        case DEVICE_ACTION_ABORT:
            return cmd_device_abort (alias, model, options->verbose);
        case DEVICE_ACTION_HOME:
            return cmd_device_home (alias, model, options->verbose);
        case DEVICE_ACTION_JOG:
            return cmd_device_jog (
                alias, model, options->jog_dx_mm, options->jog_dy_mm, options->verbose);
        case DEVICE_ACTION_VERSION:
            return cmd_device_version (alias, model, options->verbose);
        case DEVICE_ACTION_STATUS:
            return cmd_device_status (alias, model, options->verbose);
        case DEVICE_ACTION_POSITION:
            return cmd_device_position (alias, model, options->verbose);
        case DEVICE_ACTION_RESET:
            return cmd_device_reset (alias, model, options->verbose);
        case DEVICE_ACTION_REBOOT:
            return cmd_device_reboot (alias, model, options->verbose);
        default:
            return 2;
        }
    }
    case CMD_FONTS:
        return options->fonts_list
                   ? cmd_font_list_execute (options->fonts_list_families, options->verbose)
                   : 2;
    case CMD_CONFIG: {
        switch (options->config_action) {
        case CFG_SHOW:
            return cmd_config_show (NULL, options->verbose);
        case CFG_RESET:
            return cmd_config_reset (NULL, options->verbose);
        case CFG_SET:
            return cmd_config_set (options->config_set_pairs, NULL, options->verbose);
        default:
            return 2;
        }
    }

    case CMD_VERSION:
        return cmd_version_execute (options->verbose);
    default:
        return 2;
    }
}
