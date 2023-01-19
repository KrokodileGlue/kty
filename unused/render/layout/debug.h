#pragma once

#include <time.h>

enum debug_level {
        LOG_EVERYTHING,
        LOG_DETAIL,
        LOG_INFORMATION,
        LOG_CRITICAL
};

extern enum debug_level debug_level;

#define print(level,...) do {                                           \
        if (level >= debug_level) {                                     \
                char _print_buf[100];                                   \
                strftime(_print_buf, sizeof _print_buf,                 \
                         "%H:%M.%S", localtime(&(time_t){time(0)}));    \
                fprintf(stderr, "%s \e[33m%31s\e[m ",                   \
                        _print_buf, __func__);                          \
                fprintf(stderr, __VA_ARGS__);                           \
        }                                                               \
        } while (0)
