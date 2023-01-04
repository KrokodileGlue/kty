#pragma once

#include <time.h>

#define print(...) do {                                                 \
                char _print_buf[100];                                   \
                strftime(_print_buf, sizeof _print_buf,                 \
                         "%H:%M.%S", localtime(&(time_t){time(0)}));    \
                fprintf(stderr, "%s \e[33m%31s\e[m ",                   \
                        _print_buf, __func__);                          \
                fprintf(stderr, __VA_ARGS__);                           \
        } while (0)
