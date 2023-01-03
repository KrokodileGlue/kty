#pragma once

#define print(...) do {                                         \
                fprintf(stderr, "\e[33m%10s\e[m\t", __func__);    \
                fprintf(stderr, __VA_ARGS__);                   \
        } while (0)
