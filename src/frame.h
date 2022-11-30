#ifndef FRAME_H
#define FRAME_H

#include <stdint.h>

#include "util.h"
#include "sprite.h"

struct font_renderer;

enum {
        BEL = 0x07,
        BS  = 0x08,
        HT  = 0x09,
        LF  = 0x0A,
        VT  = 0x0B,
        FF  = 0x0C,
        CR  = 0x0D,
        ESC = 0x1B,
        DEL = 0x7F,
};

enum {
        CURSOR_DEFAULT  = 0,
        CURSOR_ORIGIN   = 1,
        CURSOR_WRAPNEXT = 1 << 1,
};

struct cursor {
        int x, y, mode, state;
        int fg, bg;
};

enum {
        ESC_START      = 1,
        ESC_CSI        = 1 << 1,
        ESC_STR        = 1 << 2,
        ESC_STR_END    = 1 << 3,
        ESC_ALTCHARSET = 1 << 4,
};

enum {
        MODE_CURSOR_VISIBLE = 1,
        MODE_CRLF           = 1 << 1,
        MODE_APPCURSOR      = 1 << 2,
};

struct frame {
        struct font_renderer *font;
        int cw, ch;

        /* PTY */
        int master;

        /* State */
        int col, row;
        int top, bot; /* Required for tsetscroll */
        struct cursor c, altcursor;
        struct glyph **line;

        int shell_done; /* bazinga */

        /* Escape sequence state machine */
        struct {
                char buf[1024];
                long arg[ESC_ARG_SIZE];
                unsigned len;
                int narg;
                int priv;
                int mode[2];
        } csi;

        struct {
                char buf[2048];
                unsigned len;
                unsigned char type;
                char *arg[ESC_ARG_SIZE];
                int narg;
        } esc_str;

        int mode;
        int esc;

        char *title;
        int icharset;
        int focused;

        enum {
                CURSOR_STYLE_BLINKING_BLOCK,
                CURSOR_STYLE_DEFAULT,
                CURSOR_STYLE_STEADY_BLOCK,
                CURSOR_STYLE_BLINKING_UNDERLINE,
                CURSOR_STYLE_STEADY_UNDERLINE,
                CURSOR_STYLE_BLINKING_BAR,
                CURSOR_STYLE_STEADY_BAR,
                CURSOR_STYLE_MAX = CURSOR_STYLE_STEADY_BAR,
        } cursor_style;

        struct global *k;
};

struct frame *frame_new(char **env, struct font_renderer *f);
void frame_title(struct frame *f, const char *title);

#endif
