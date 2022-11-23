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
        strftime(buf, sizeof buf, "%M:%S", t);
        fprintf(stderr, "%s ", buf);
        vfprintf(stderr, fmt, args);
        va_end(args);
}

#include "util.h"
