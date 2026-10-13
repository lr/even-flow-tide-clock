/**
 * @file config_check.c
 * Validate settings text the way the board will, for the dev server.
 *
 *   even_flow_config_check < settings.txt
 *
 * Exit 0: prints the settings in canonical form (what gets stored).
 * Exit 1: prints "N problem(s): <first problem>" and stores nothing.
 */
#include <stdio.h>

#include "config/config.h"

int main(void)
{
    static char text[8192];
    size_t len = fread(text, 1, sizeof(text) - 1, stdin);
    if(!feof(stdin)) {
        printf("1 problem(s): settings too long\n");
        return 1;
    }
    text[len] = '\0';

    clock_config_t cfg;
    config_defaults(&cfg);
    char err[96];
    int errors = config_parse(&cfg, text, err, sizeof(err));
    if(errors > 0) {
        printf("%d problem(s): %s\n", errors, err);
        return 1;
    }

    char out[CONFIG_TEXT_MAX];
    if(config_format(&cfg, out, sizeof(out)) == 0) {
        printf("1 problem(s): could not format settings\n");
        return 1;
    }
    fputs(out, stdout);
    return 0;
}
