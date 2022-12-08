#include "term.h"

#include <stdlib.h>                    /* NULL, calloc, free, grantpt */
#include <string.h>                    /* strcpy, strlen */

#include "global.h"                    /* global_notify_title_change */
#include "t.h"
#include "platform.h"
#include "esc.h"

extern struct global *k;

struct term *term_new()
{
        /* The shell is running, now set up the window/graphics. */
        struct term *t = calloc(1, sizeof *t);

        if (!t) return NULL;

        t->mode = MODE_CURSOR_VISIBLE;

        t->c[0].fg = t->c[0].bg = -1;
        t->c[1].fg = t->c[1].bg = -1;

        t->g = t->grid;

        return t;
}

void term_title(struct term *t, const char *title)
{
        free(t->title);
        t->title = malloc(strlen(title) + 1); /* TODO: xmalloc */
        strcpy(t->title, title);
        global_notify_title_change(t);
}

void term_resize(struct term *t, int col, int row)
{
        tresize(t, col, row);
}
