#include "internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int g_level = 2; /* 0 trace .. 4 error; default info */

void cforge_log_set_level(int level) {
    g_level = level;
}

void cforge_logf(int level, const char *fmt, ...) {
    if (level < g_level) {
        return;
    }
    const char *tag = "INFO";
    if (level >= 4) {
        tag = "ERROR";
    } else if (level == 3) {
        tag = "WARN";
    } else if (level == 1) {
        tag = "DEBUG";
    }
    fprintf(stderr, "%s ", tag);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
