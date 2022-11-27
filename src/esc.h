#ifndef ESC_H
#define ESC_H

#include <stdint.h>

struct frame;

int eschandle(struct frame *f, uint32_t c);
void resetesc(struct frame *f);
void resetcsi(struct frame *f);
void csihandle(struct frame *f);
void handle_terminal_mode(struct frame *f, int set);
void csiparse(struct frame *f);

#endif
