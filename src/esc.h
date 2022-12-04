#ifndef ESC_H
#define ESC_H

#include <stdint.h>
#include <stdbool.h>

struct term;

int eschandle(struct term *f, uint32_t c);
void resetesc(struct term *f);
void resetcsi(struct term *f);
void csiparse(struct term *f);

#endif
