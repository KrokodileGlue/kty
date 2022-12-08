#include "term.h"
#include "t.h"

void term_init(struct term *t)
{
        memset(t, 0, sizeof *t);
        t->mode = MODE_CURSOR_VISIBLE;
        t->c[0].fg = t->c[0].bg = -1;
        t->c[1].fg = t->c[1].bg = -1;
        t->g = t->grid;
}

void term_resize(struct term *t, int col, int row)
{
        tresize(t, col, row);
}
