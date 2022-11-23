#ifndef UTF8_H
#define UTF8_H

#define UTF8CONT(X) (((uint8_t)(X) & 0xc0) == 0x80)

unsigned utf8chrlen(const char *s, unsigned l);
unsigned utf8decode(const char *s, unsigned l, uint32_t *c);
int utf8encode(uint32_t c, uint8_t *buf, unsigned *len);

#endif
