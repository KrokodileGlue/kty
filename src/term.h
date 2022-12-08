#pragma once

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
        MODE_CURSOR_VISIBLE = 1 << 0,
        MODE_CRLF           = 1 << 1,
        MODE_APPCURSOR      = 1 << 2,
        MODE_WRAP           = 1 << 3, /* Whether to wrap lines or truncate */
        MODE_ALTSCREEN      = 1 << 4,
};

struct term {
        struct cursor c[2];

        struct grid {
                uint32_t **line;
                struct cell_attr { int mode, fg, bg; } **attr;
                bool *wrap;

                int row, col;
                int top, bot;

                /*
                 * In st swapping the screen resets the charset
                 * because the charset is considered a part of the
                 * global state. In kitty each screen remembers its
                 * own charset when you swap back and forth. We'll go
                 * with kitty's behavior and make the charset a
                 * per-screen state for no reason in particular.
                 */
                int charset;
        } grid[2];

        enum {
                ESC_START      = 1 << 0, /* Always set during parsing */
                ESC_CSI        = 1 << 1, /* Always set during CSI parsing */
                ESC_ALTCHARSET = 1 << 2, /* Always set during ... parsing */
                ESC_STR        = 1 << 3, /* Always set during ... parsing */
                ESC_STR_END    = 1 << 4, /* Set after string termination */
        } esc;

        struct csi csi;
        struct stresc stresc;

        /*
         * There are two grids: the primary grid and the alternate
         * grid. All operations happen on the primary grid, which this
         * points to.
         */
        struct grid *g;

        int mode;

        char *title;
};

struct term *term_new();
void term_title(struct term *f, const char *title);
void term_resize(struct term *t, int col, int row);
