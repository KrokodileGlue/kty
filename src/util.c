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
        fprintf(stdout, "%s \e[32m%12s\e[0m ", buf, func);
        vfprintf(stdout, fmt, args);
        va_end(args);
}
