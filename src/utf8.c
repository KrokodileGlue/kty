#include <stdint.h>

#include "utf8.h"

#define UTF8CONT(X) (((uint8_t)(X) & 0xc0) == 0x80)

unsigned utf8chrlen(const char *s, unsigned l)
{
        unsigned i = 0;
        while (s++ && ++i < l && UTF8CONT(*s));
        return i;
}

unsigned utf8decode(const char *s, unsigned l, uint32_t *c)
{
        unsigned len = utf8chrlen(s, l);

        *c = (*s & ((1 << (8 - len)) - 1))
                << (len - 1) * 6;
        for (unsigned i = 1; i < len; i++)
                *c |= (s[i] & 0x3F) << (len - i - 1) * 6;

        return len;
}

int utf8encode(uint32_t c, uint8_t *buf, unsigned *len)
{
        if (c <= 0x7F) {
                *len = 1;
                buf[0] = c & 0xFF;
        } else if (c >= 0x80 && c <= 0x7FF) {
                *len = 2;
                buf[0] = 0xC0 | ((c >> 6) & 0x1F);
                buf[1] = 0x80 | (c & 0x3F);
        } else if (c >= 0x800 && c <= 0xFFFF) {
                *len = 3;
                buf[0] = 0xE0 | ((c >> 12) & 0xF);
                buf[1] = 0x80 | ((c >> 6) & 0x3F);
                buf[2] = 0x80 | (c & 0x3F);
        } else if (c >= 0x10000 && c <= 0x10FFFF) {
                *len = 4;
                buf[0] = 0xF0 | ((c >> 18) & 0x7);
                buf[1] = 0x80 | ((c >> 12) & 0x3F);
                buf[2] = 0x80 | ((c >> 6) & 0x3F);
                buf[3] = 0x80 | (c & 0x3F);
        } else return 1;

        return 0;
}
