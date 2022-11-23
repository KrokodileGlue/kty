#include <stdint.h>

#include <GL/glew.h>
#include <GL/gl.h>

#include "util.h"
#include "frame.h"
#include "util.h"
#include "esc.h"
#include "t.h"

void csiparse(struct frame *f)
{
	char *p = f->csi.buf, *np;
	long v;

	f->csi.narg = 0;
	if (*p == '?') {
		f->csi.priv = 1;
		p++;
	}

	f->csi.buf[f->csi.len] = 0;
	while (p < f->csi.buf + f->csi.len) {
		np = NULL;
		v = strtol(p, &np, 10);
		if (np == p)
			break;
		if (v == LONG_MAX || v == LONG_MIN)
			v = -1;
		f->csi.arg[f->csi.narg++] = v;
		p = np;
		if ((*p != ';' && *p != ':') || f->csi.narg == ESC_ARG_SIZE)
			break;
		p++;
	}

	f->csi.mode[0] = *p++;
	f->csi.mode[1] = (p < f->csi.buf + f->csi.len) ? *p : 0;

        _printf("(%.*s) -> ESC '[' [[ [<priv:%d>] ", f->csi.len, f->csi.buf, f->csi.priv);
        for (int i = 0; i < f->csi.narg; i++)
                _printf("<arg:%ld> [;]", f->csi.arg[i]);
        _printf("] <mode:%d:%c> [<mode:%d>]]\n", f->csi.mode[0], f->csi.mode[0], f->csi.mode[1]);
}

void get_csi_graphic_mode(long arg, int *mode)
{
        switch (arg) {
        case 0:
                *mode = 0;
                break;
        case 1: /* Bold */
                *mode |= GLYPH_BOLD;
                break;
        case 22: /* Turn off bold */
                *mode &= ~GLYPH_BOLD;
                break;
        case 4: /* Underline */
                *mode |= GLYPH_UNDERLINE;
                break;
        case 24: /* Turn off underline */
                *mode &= ~GLYPH_UNDERLINE;
                break;
        case 7:
                *mode |= GLYPH_INVERSE;
                break;
        case 27:
                *mode &= ~GLYPH_INVERSE;
                break;
        default:
                fprintf(stderr, "Unknown CSI sequence argument %ld\n", arg);
                break;
        }
}

void handle_terminal_mode(struct frame *f, int set)
{
        int mode = 0;

        switch (f->csi.arg[0]) {
        case 25: /* Make cursor visible */
                mode |= MODE_CURSOR_VISIBLE;
                break;
        }

        if (set) f->mode |= mode;
        else f->mode &= ~mode;
}

#define DEFAULT(x, y) (f->csi.narg ? (x) : (y))

void csihandle(struct frame *f)
{
        switch (f->csi.mode[0]) {
        case 'A': /* Move cursor up n lines */
                tmoveto(f, f->c.x, f->c.y - DEFAULT(f->csi.arg[0], 1));
                break;
        case 'B': /* Move cursor down n lines */
                tmoveto(f, f->c.x, f->c.y + DEFAULT(f->csi.arg[0], 1));
                break;
        case 'C': /* Move cursor right n lines */
                tmoveto(f, f->c.x + DEFAULT(f->csi.arg[0], 1), f->c.y);
                break;
        case 'D': /* Move cursor left n lines */
                tmoveto(f, f->c.x - DEFAULT(f->csi.arg[0], 1), f->c.y);
                break;
        case 'h': /* Set terminal mode */
                handle_terminal_mode(f, 1);
                break;
        case 'H': /* CUP - Move cursor too coordinates */
                if (!f->csi.narg)
                        tmoveto(f, 0, 0);
                else
                        tmoveto(f, f->csi.arg[1] - 1, f->csi.arg[0] - 1);
                break;
        case 'J': /* Clear screen */
                switch (f->csi.arg[0]) {
                case 0: /* below */
                        tclearregion(f, f->c.x, f->c.y, f->col - 1, f->c.y);
                        if (f->c.y < f->row - 1) {
                                tclearregion(f, 0, f->c.y + 1, f->col - 1, f->row - 1);
                        }
                        break;
                case 1: /* above */
                        if (f->c.y > 1)
                                tclearregion(f, 0, 0, f->col - 1, f->c.y - 1);
                        tclearregion(f, 0, f->c.y, f->c.x, f->c.y);
                        break;
                case 2: /* all */
                        tclearregion(f, 0, 0, f->col - 1, f->row - 1);
                        break;
                default:
                        fprintf(stderr, "Unknown clear argument %ld\n", f->csi.arg[0]);
                        break;
                }
                break;
        case 'K': /* EL - Clear line */
                if (!f->csi.narg || f->csi.arg[0] == 0)
                        tclearregion(f, f->c.x, f->c.y, f->col - 1, f->c.y);
                else if (f->csi.arg[0] == 1)
                        tclearregion(f, 0, f->c.y, f->c.x, f->c.y);
                else if (f->csi.arg[0] == 2)
                        tclearregion(f, 0, f->c.y, f->col - 1, f->c.y);
                break;
        case 'l': /* Reset terminal mode */
                handle_terminal_mode(f, 0);
                break;
        case 'L':
                tscrolldown(f, f->c.y, f->csi.narg ? f->csi.arg[0] : 1);
                break;
        case 'm':
                if (!f->csi.narg) {
                        get_csi_graphic_mode(0, &f->c.mode);
                } else {
                        for (int i = 0; i < f->csi.narg; i++)
                                get_csi_graphic_mode(f->csi.arg[i], &f->c.mode);
                }
                break;
        case 'M': /* DL - Delete n lines */
                tscrollup(f, f->c.y, f->csi.narg ? f->csi.arg[0] : 1);
                break;
        case 'r': /* DECSTBM - Set scroll region */
                if (!f->csi.narg) tsetscroll(f, 0, f->row - 1);
                else tsetscroll(f, f->csi.arg[0] - 1, f->csi.arg[1] - 1);
                tmoveato(f, 0, 0);
                break;
        case 'X': /* ECH - Erase n chars */
                tclearregion(f, f->c.x, f->c.y,
                        f->c.x + (f->csi.narg ? f->csi.arg[0] : 1), f->c.y);
                break;
        case ' ':
                if (f->csi.mode[1] == 'q') {
                        /* TODO: Set cursor style. */
                }
                break;
        default:
                fprintf(stderr, "Unhandled escape sequence\n");
                break;
        }
}

void resetcsi(struct frame *f)
{
        memset(&f->csi, 0, sizeof f->csi);
}

void resetesc(struct frame *f)
{
        f->esc = 0;
}

void eschandle(struct frame *f, uint32_t c)
{
        switch (c) {
        case '[':
                f->esc |= ESC_CSI;
                break;
        }
}
