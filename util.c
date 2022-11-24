#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

void _printf(const char *func, const char *fmt, ...)
{
        (void)func;
        va_list args;
        va_start(args, fmt);
        struct tm *t = localtime(&(time_t){time(NULL)});
        char buf[BUFSIZ];
        strftime(buf, sizeof buf, "%H:%M:%S", t);
        fprintf(stderr, "%s \x1b[32m%12s\x1b[0m ", buf, func);
        vfprintf(stderr, fmt, args);
        va_end(args);
}

#include "util.h"
