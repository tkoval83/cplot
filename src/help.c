/**
 * @file help.c
 * @brief Тексти довідки для інтерактивного CLI.
 */
#include <stdio.h>

#include "colors.h"
#include "help.h"
#include "proginfo.h"

void usage (void) {
    fprintf (stdout, BROWN "Використання:" NO_COLOR "\n");
    fprintf (stdout, "  %s            # інтерактивний режим\n", __PROGRAM_NAME__);
    fprintf (stdout, "  %s --mcp      # сервер MCP поверх stdin/stdout\n\n", __PROGRAM_NAME__);
}

void description (void) {
    fprintf (stdout, BROWN "Опис:" NO_COLOR "\n");
    fprintf (
        stdout,
        "Програма відкриває інтерактивну оболонку керування AxiDraw MiniKit 2. "
        "Усі команди (connect, pen, jog тощо) вводяться вже у цій оболонці.\n");
    fprintf (
        stdout,
        "Для інтеграції з інструментами автоматизації використовуйте режим MCP, "
        "який приймає запити JSON-RPC через stdin/stdout.\n\n");
}

void options (void) {
    fprintf (stdout, BROWN "Прапорці CLI:" NO_COLOR "\n");
    fprintf (stdout, "  --mcp      — запустити MCP-сервер (без оболонки)\n");
    fprintf (stdout, "  --help     — показати цю довідку\n");
    fprintf (stdout, "  --version  — показати версію програми\n\n");
}

void author (void) {
    fprintf (stdout, BROWN "Автор:" NO_COLOR " %s\n\n", __PROGRAM_AUTHOR__);
}

void version (void) {
    fprintf (stdout, __PROGRAM_NAME__ " версія: " GRAY "%s\n" NO_COLOR, __PROGRAM_VERSION__);
}

void help (void) {
    fprintf (stdout, BLUE __PROGRAM_NAME__ "\n\n" NO_COLOR);
    usage ();
    description ();
    options ();
    author ();
}
