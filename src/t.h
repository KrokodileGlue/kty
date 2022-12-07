#ifndef T_H
#define T_H

#include <stdint.h>
#include <stdbool.h>

#include "esc.h"

struct term;
struct grid;

/*
 * There should be only one of these functions available: twrite. The
 * rest should be internal.
 */

int twrite(struct term *t, const char *buf, int buflen);
void tresize(struct term *t, int col, int row);

#endif
