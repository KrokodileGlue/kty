#include "term.h"

#include <stdlib.h>                    /* NULL, calloc, free, grantpt */
#include <string.h>                    /* strcpy, strlen */

#include "global.h"                    /* global_notify_title_change */
#include "t.h"
#include "platform.h"

extern struct global *k;

struct term *term_new(struct font_renderer *r, int width, int height)
{
        /* The shell is running, now set up the window/graphics. */
        struct term *f = calloc(1, sizeof *f);

        if (!f) return NULL;

        f->mode = MODE_CURSOR_VISIBLE;
        f->c.fg = f->c.bg = -1;
        f->k = k;
        f->font = r;
        f->width = width;
        f->height = height;

        glGenFramebuffers(1, &f->framebuffer);

        glBindFramebuffer(GL_FRAMEBUFFER, f->framebuffer);

        /* Set up the texture */
        glGenTextures(1, &f->tex_color_buffer);
        glBindTexture(GL_TEXTURE_2D, f->tex_color_buffer);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        /* TODO: Don't use the window width and height here. */
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, r->width, r->height, 0,
                GL_RGB, GL_UNSIGNED_BYTE, NULL);

        glBindTexture(GL_TEXTURE_2D, 0);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D, f->tex_color_buffer, 0);

        GLuint status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
                _printf("Framebuffer status: %u\n", status);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        term_set_font_size(f, 12);
        f->subprocess = platform_spawn_shell();
        platform_inform_subprocess_of_resize(f->subprocess, 1, 1);

        return f;
}

void term_set_font_size(struct term *f, int font_size)
{
        f->font_size = font_size;

        /*
         * Hacky; this assumes that the first font in the list is the
         * user's primary font and that the font is monospace.
         */
        FT_Face face = f->font->m->fonts[0].face;
        FT_Set_Pixel_Sizes(face, 0, font_size);
        FT_Load_Char(face, 'x', FT_LOAD_COMPUTE_METRICS);
        FT_GlyphSlot slot = face->glyph;
        f->cw = slot->metrics.horiAdvance / 64.0;
        f->ch = slot->metrics.vertAdvance / 64.0;

        f->top = 0, f->bot = f->height / (f->ch + LINE_SPACING) - 1;

        tresize(f, f->width / f->cw, f->height / (f->ch + LINE_SPACING));
}

void term_title(struct term *f, const char *title)
{
        free(f->title);
        f->title = malloc(strlen(title) + 1); /* TODO: xmalloc */
        strcpy(f->title, title);
        global_notify_title_change(f);
}
