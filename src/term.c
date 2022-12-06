#include "term.h"

#include <stdlib.h>                    /* NULL, calloc, free, grantpt */
#include <string.h>                    /* strcpy, strlen */

#include "global.h"                    /* global_notify_title_change */
#include "t.h"
#include "platform.h"

extern struct global *k;

struct term *term_new(int width, int height)
{
        /* The shell is running, now set up the window/graphics. */
        struct term *f = calloc(1, sizeof *f);

        if (!f) return NULL;

        f->mode = MODE_CURSOR_VISIBLE;

        f->grid[0].cursor[0].fg = f->grid[0].cursor[0].bg = -1;
        f->grid[0].cursor[1].fg = f->grid[0].cursor[1].bg = -1;
        f->grid[1].cursor[0].fg = f->grid[1].cursor[0].bg = -1;
        f->grid[1].cursor[1].fg = f->grid[1].cursor[1].bg = -1;

        f->grid[0].c = f->grid[0].cursor;
        f->grid[1].c = f->grid[1].cursor;

        f->g = f->grid;

        f->width = width;
        f->height = height;

        /* TODO: None of this should be here. */
        glGenFramebuffers(1, &f->framebuffer);

        glBindFramebuffer(GL_FRAMEBUFFER, f->framebuffer);

        /* Set up the texture */
        glGenTextures(1, &f->tex_color_buffer);
        glBindTexture(GL_TEXTURE_2D, f->tex_color_buffer);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        /* TODO: Don't use the window width and height here. */
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0,
                GL_RGB, GL_UNSIGNED_BYTE, NULL);

        glBindTexture(GL_TEXTURE_2D, 0);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D, f->tex_color_buffer, 0);

        GLuint status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
                _printf("Framebuffer status: %u\n", status);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        return f;
}

void term_set_font_size(struct term *f, int cw, int ch)
{
        f->cw = cw, f->ch = ch;
        f->g->top = 0, f->g->bot = f->height / (f->ch + LINE_SPACING) - 1;
        tresize(f->g, f->width / f->cw, f->height / (f->ch + LINE_SPACING));
}

void term_title(struct term *f, const char *title)
{
        free(f->title);
        f->title = malloc(strlen(title) + 1); /* TODO: xmalloc */
        strcpy(f->title, title);
        global_notify_title_change(f);
}

void term_resize(struct term *t, int width, int height)
{
        t->width = width, t->height = height;
        t->g->top = 0, t->g->bot = height / (t->ch + LINE_SPACING) - 1;
        tresize(t->g, width / t->cw, height / (t->ch + LINE_SPACING));
}
