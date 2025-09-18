/**
 * @file help.c
 * @brief Реалізація генерації довідки на основі argdefs.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "colors.h"
#include "config.h"
#include "help.h"
#include "proginfo.h"

void usage (void) {
    fprintf (stdout, BROWN "Використання: " NO_COLOR);
    fprintf (stdout, "%s <команда> [опції] [вхід]\n\n", __PROGRAM_NAME__);
    fprintf (stdout, "Команди:\n");
    size_t ncmd = 0;
    const cli_command_desc_t *cmds = argdefs_commands (&ncmd);
    for (size_t i = 0; i < ncmd; ++i) {
        fprintf (stdout, "  %-7s %s\n", cmds[i].name, cmds[i].description);
    }
    fputc ('\n', stdout);
}

void description (void) {
    fprintf (stdout, BROWN "Опис: " NO_COLOR);
    fprintf (
        stdout,
        "Плотинг Markdown/звичайного тексту на AxiDraw MiniKit 2 з детермінованою розкладкою.\n");
    fprintf (stdout, "\nПриклади використання конфігурації:\n");
    fprintf (
        stdout, "  cplot config            # показати активні типові значення та шлях до "
                "файлу конфігурації\n");
    fprintf (
        stdout, "  CPLOT_RESET_DEFAULTS=1 cplot config  # скинути до заводських налаштувань\n");
}

void options (void) {
    fprintf (stdout, BROWN "Опції:\n\n" NO_COLOR);

    size_t nopt = 0;
    const cli_option_desc_t *opts = argdefs_options (&nopt);

    const char *groups[] = { "global", "input", "layout", "device", "config" };
    const char *titles[]
        = { "  Глобальні:", "  Джерело даних (print):", "  Розкладка (print/preview):",
            "  підкоманда device:", "  підкоманда config:" };
    for (size_t g = 0; g < 5; ++g) {
        fprintf (stdout, "%s\n", titles[g]);
        for (size_t i = 0; i < nopt; ++i) {
            if (opts[i].group && strcmp (opts[i].group, groups[g]) == 0) {
                if (opts[i].short_name && opts[i].short_name != '\0') {
                    fprintf (stdout, "    -%c|--%-20s", opts[i].short_name, opts[i].long_name);
                } else {
                    fprintf (stdout, "    --%-23s", opts[i].long_name);
                }
                if (opts[i].arg_placeholder) {
                    fprintf (stdout, " %-12s", opts[i].arg_placeholder);
                } else {
                    fprintf (stdout, " %-12s", "");
                }
                fprintf (stdout, " %s\n", opts[i].description ? opts[i].description : "");
            }
        }
        fputc ('\n', stdout);
    }
}

void author (void) { fprintf (stdout, BROWN "Автор: " GRAY "%s\n\n" NO_COLOR, __PROGRAM_AUTHOR__); }

void version (void) {
    fprintf (stdout, __PROGRAM_NAME__ " версія: " GRAY "%s\n" NO_COLOR, __PROGRAM_VERSION__);
}

void help (void) {
    fprintf (stdout, BLUE __PROGRAM_NAME__ "\n\n" NO_COLOR);
    usage ();
    description ();
    options ();
    // Перелік підтримуваних пристроїв та ключів конфігурації/типових значень
    fprintf (stdout, BROWN "Підтримувані пристрої:" NO_COLOR "\n");
    fprintf (
        stdout, "  minikit2  (AxiDraw MiniKit 2; 160×101 мм, 80 кроків/мм, 10 дюйм/с макс)\n\n");

    fprintf (stdout, BROWN "Ключі конфігурації (config --set):" NO_COLOR "\n");
    size_t nkeys = 0;
    const cli_config_desc_t *keys = argdefs_config_keys (&nkeys);
    config_t def;
    config_factory_defaults (&def, CONFIG_DEFAULT_MODEL);
    for (size_t i = 0; i < nkeys; ++i) {
        const cli_config_desc_t *d = &keys[i];
        fprintf (stdout, "  %-16s", d->key);
        if (d->type == CFGK_ENUM && d->enum_values) {
            fprintf (stdout, "=%s", d->enum_values);
        } else if (d->unit) {
            fprintf (stdout, "=%s", d->unit);
        } else {
            fprintf (stdout, "=");
        }

        // Вивести типове значення
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
        const char *fmt = d->fmt ? d->fmt : "%g";
        if (d->type == CFGK_INT) {
            const int *p = (const int *)((const char *)&def + d->offset);
            fprintf (stdout, " (типово ");
            fprintf (stdout, fmt, *p);
            fprintf (stdout, ")");
        } else if (d->type == CFGK_DOUBLE) {
            const double *p = (const double *)((const char *)&def + d->offset);
            fprintf (stdout, " (типово ");
            fprintf (stdout, fmt, *p);
            fprintf (stdout, ")");
        } else if (d->type == CFGK_ENUM) {
            // special-case orientation mapping to text
            if (strcmp (d->key, "orient") == 0) {
                fprintf (
                    stdout, " (типово %s)",
                    (def.orientation == ORIENT_LANDSCAPE) ? "landscape" : "portrait");
            }
        }
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        if (d->description && d->description[0]) {
            fprintf (stdout, " — %s", d->description);
        }
        fputc ('\n', stdout);
    }
    fputc ('\n', stdout);

    author ();
}
