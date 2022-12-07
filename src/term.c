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
        struct term *t = calloc(1, sizeof *t);

        if (!t) return NULL;

        t->mode = MODE_CURSOR_VISIBLE;

        t->cursor[0].fg = t->cursor[0].bg = -1;
        t->cursor[1].fg = t->cursor[1].bg = -1;
        t->c = t->cursor;

        t->g = t->grid;

        t->width = width;
        t->height = height;

        /* TODO: None of this should be here. */
        glGenFramebuffers(1, &t->framebuffer);

        glBindFramebuffer(GL_FRAMEBUFFER, t->framebuffer);

        /* Set up the texture */
        glGenTextures(1, &t->tex_color_buffer);
        glBindTexture(GL_TEXTURE_2D, t->tex_color_buffer);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        /* TODO: Don't use the window width and height here. */
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0,
                GL_RGB, GL_UNSIGNED_BYTE, NULL);

        glBindTexture(GL_TEXTURE_2D, 0);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D, t->tex_color_buffer, 0);

        GLuint status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
                _printf("Framebuffer status: %u\n", status);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        return t;
}

void term_set_font_size(struct term *t, int cw, int ch)
{
        t->cw = cw, t->ch = ch;
        tresize(t, t->width / t->cw, t->height / (t->ch + LINE_SPACING));
}

void term_title(struct term *t, const char *title)
{
        free(t->title);
        t->title = malloc(strlen(title) + 1); /* TODO: xmalloc */
        strcpy(t->title, title);
        global_notify_title_change(t);
}

void term_resize(struct term *t, int width, int height)
{
        t->width = width, t->height = height;
        t->g->top = 0, t->g->bot = height / (t->ch + LINE_SPACING) - 1;
        tresize(t, width / t->cw, height / (t->ch + LINE_SPACING));
}
