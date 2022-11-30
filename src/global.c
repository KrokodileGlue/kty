#define _XOPEN_SOURCE 600

#include "global.h"

#include <GL/glew.h>            /* GLfloat, glTexParameteri, GL_ARRAY_BU... */
#include <freetype/freetype.h>  /* FT_GlyphSlotRec_, FT_FaceRec_, FT_Gly... */
#include <freetype/ftimage.h>   /* FT_Bitmap */
#include <freetype/fttypes.h>   /* FT_MAKE_TAG */
#include <freetype/tttables.h>  /* FT_Load_Sfnt_Table */
#include <inttypes.h>           /* uint32_t */
#include <stdio.h>              /* fprintf, stderr, NULL */
#include <stdlib.h>             /* calloc, abs */
#include <string.h>             /* memcpy */
#include <wchar.h>              /* wcwidth */
#include "font.h"               /* font, glyph, GLYPH_DUMMY, GLYPH_INVERSE */
#include "frame.h"              /* frame, window, frame_new, cursor, MOD... */
#include "render.h"             /* font_renderer */
#include "sprite.h"             /* sprite */
#include "util.h"               /* color, LINE_SPACING, NUM_GLYPH, FONT_... */
#include "utf8.h"
#include "render.h"

int global_init(struct global *k, char **env, void (*window_title_callback)(char *))
{
        int cw, ch;
        font_manager_init(&k->m, &cw, &ch);
        render_init(&k->font, &k->m);

        struct frame *f = frame_new(env, &k->font);

        f->focused = 1;
        f->cw = cw;
        f->ch = ch;

        k->frame[k->nframe++] = f;

        k->window_title_callback = window_title_callback;
        k->focus = f;

        return 0;
}

int global_notify_title_change(struct frame *f)
{
        struct global *k = f->k;
        if (f == k->focus)
                k->window_title_callback(f->title);
        return 0;
}

int global_render(struct global *f)
{
        /* TODO */
        render_frame(&f->font, f->frame[0]);
        return 0;
}
