#ifndef FRAME_H
#define FRAME_H

#include <stdint.h>
#include <stdbool.h>

#include <GL/glew.h>

#include "util.h"
#include "sprite.h"
#include "esc.h"

struct cursor {
        int x, y, mode, state;
        int fg, bg;

        enum {
                CURSOR_STYLE_BLINKING_BLOCK,
                CURSOR_STYLE_DEFAULT,
                CURSOR_STYLE_STEADY_BLOCK,
                CURSOR_STYLE_BLINKING_UNDERLINE,
                CURSOR_STYLE_STEADY_UNDERLINE,
                CURSOR_STYLE_BLINKING_BAR,
                CURSOR_STYLE_STEADY_BAR,
                CURSOR_STYLE_MAX = CURSOR_STYLE_STEADY_BAR,
        } style;
};

enum {
        ESC_START      = 1 << 0,
        ESC_CSI        = 1 << 1,
        ESC_STR        = 1 << 2,
        ESC_STR_END    = 1 << 3,
        ESC_ALTCHARSET = 1 << 4,
};

enum {
        MODE_CURSOR_VISIBLE = 1 << 0,
        MODE_CRLF           = 1 << 1,
        MODE_APPCURSOR      = 1 << 2,
        MODE_WRAP           = 1 << 3, /* Whether to wrap lines or truncate */
        MODE_ALTSCREEN      = 1 << 4,
};

struct term {
        int cw, ch;             /* Character width/height */
        int width, height;      /* Dimensions in pixels */

        /* State */

        struct cursor cursor[2];
        struct cursor *c;

        struct grid {
                uint32_t **line;
                struct cell_attr { int mode, fg, bg; } **attr;
                bool *wrap;

                int row, col;
                int top, bot;

                int esc;        /* Escape state */
                int charset;

                struct csi csi;
                struct stresc stresc;
        } grid[2];

        /*
         * There are two grids: the primary grid and the alternate
         * grid. All operations happen on the primary grid, which this
         * points to.
         */
        struct grid *g;

        int mode;

        char *title;

        /* TODO: Move these out into window.h */
        GLuint framebuffer;
        GLuint tex_color_buffer;
};

struct term *term_new(int width, int height);
void term_title(struct term *f, const char *title);
void term_set_font_size(struct term *f, int cw, int ch);
void term_resize(struct term *t, int width, int height);

#endif
