#include "esc.h"

#include <limits.h>  /* LONG_MAX, LONG_MIN */
#include <stdint.h>  /* uint32_t */
#include <stdio.h>   /* NULL */
#include <stdlib.h>  /* strtol */
#include <string.h>  /* memset */
#include <unistd.h>

#include "frame.h"   /* frame, frame::(anonymous), cursor, ESC_ALTCHARSET */
#include "render.h"  /* font_renderer */
#include "t.h"       /* tclearregion, tmoveto, tscrolldown, tsetscroll */
#include "util.h"    /* _printf, ESC_ARG_SIZE */

void csiparse(struct frame *f)
{
        f->csi.narg = 0;
        f->csi.buf[f->csi.len] = 0;

        char *p = f->csi.buf;

        if (*p == '?') {
                f->csi.priv = 1;
                p++;
        }

        while (p < f->csi.buf + f->csi.len) {
                char *np = NULL;
                long v = strtol(p, &np, 10);
                if (np == p) break;
                if (v == LONG_MAX || v == LONG_MIN) v = -1;
                f->csi.arg[f->csi.narg++] = v;
                p = np;

                /*
                 * TODO: Investigate why some applications seem to use : instad
                 * of ; for some sequences.
                 */
                if ((*p != ';' && *p != ':') || f->csi.narg == ESC_ARG_SIZE)
                        break;
                p++;
        }

        f->csi.mode[0] = *p++;
        f->csi.mode[1] = (p < f->csi.buf + f->csi.len) ? *p : 0;

#ifdef DEBUG
        static char buf[2048] = { 0 };
        snprintf(buf, sizeof buf, "%.*s%s -> ", f->csi.len, f->csi.buf, f->csi.priv ? " (priv)" : "");
        for (int i = 0; i < f->csi.narg; i++)
                snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "%ld ", f->csi.arg[i]);
        snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "mode %c mode %d\n", f->csi.mode[0], f->csi.mode[1]);
        _printf("%s", buf);
#endif
}

/*
 * Handles the 'h' escape code.
 */
void handle_terminal_mode(struct frame *f, int set)
{
        int mode = 0;

        switch (f->csi.arg[0]) {
        case 1:                 /* DECCKM - Cursor key */
                mode |= MODE_APPCURSOR;
                break;
        case 7:                 /* DECAWM - Autowrap Mode */
                mode |= MODE_WRAP;
                break;
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
        case '@': /* ICH -- Insert <n> blank char */
                tinsertblank(f, f->csi.narg ? f->csi.arg[0] : 1);
                break;
        case 'A': /* Move cursor up n lines */
                tmoveto(f, f->c.x, f->c.y - DEFAULT(f->csi.arg[0], 1));
                break;
        case 'B': /* Move cursor down n lines */
                tmoveto(f, f->c.x, f->c.y + DEFAULT(f->csi.arg[0], 1));
                break;
        case 'C': /* Move cursor right n columns */
                tmoveto(f, f->c.x + DEFAULT(f->csi.arg[0], 1), f->c.y);
                break;
        case 'c':
                write(f->master, VT_IDENTITY, strlen(VT_IDENTITY));
                break;
        case 'D': /* Move cursor left n columns */
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
                        _printf("Unknown clear argument %ld\n", f->csi.arg[0]);
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
        case 'L': /* IL - Insert n blank lines */
                tscrolldown(f, f->c.y, f->csi.narg ? f->csi.arg[0] : 1);
                break;
        case 'm':
                tsetattr(f);
                break;
        case 'M': /* DL - Delete n lines */
                tscrollup(f, f->c.y, f->csi.narg ? f->csi.arg[0] : 1);
                break;
        case 'P': /* DCH - Delete n chars */
                tdeletechar(f, f->csi.narg ? f->csi.arg[0] : 1);
                break;
	case 'S': /* SU - Scroll n line up */
		tscrollup(f, f->top, f->csi.narg ? f->csi.arg[0] : 1);
		break;
	case 's': /* DECSC - Save cursor position */
	case 'u': /* DECRC - Restore cursor position */
		tcursor(f);
		break;
	case 'T': /* SD - Scroll n line down */
		tscrolldown(f, f->top, f->csi.narg ? f->csi.arg[0] : 1);
		break;
        case 'r': /* DECSTBM - Set scroll region */
                if (!f->csi.narg) tsetscroll(f, 0, f->row - 1);
                else tsetscroll(f, f->csi.arg[0] - 1, f->csi.arg[1] - 1);
                tmoveato(f, 0, 0);
                break;
        case 'd': /* VPA - Move to <row> */
                tmoveato(f, f->c.x, (f->csi.narg ? f->csi.arg[0] : 1) - 1);
                break;
        case 'G': /* CHA - Move to <col> */
        case '`': /* HPA */
                tmoveato(f, (f->csi.narg ? f->csi.arg[0] : 1) - 1, f->c.y);
                break;
        case 'X': /* ECH - Erase n chars */
                tclearregion(f, f->c.x, f->c.y,
                             f->c.x + (f->csi.narg ? f->csi.arg[0] : 1), f->c.y);
                break;
        case ' ':
                if (f->csi.arg[0] < 0 || f->csi.arg[0] > CURSOR_STYLE_MAX)
                        goto unhandled;
                if (f->csi.mode[1] == 'q') {
                        f->cursor_style = f->csi.arg[0];
                        f->font->dirty_display = 1;
                        break;
                }
        default:
                goto unhandled;
        }

        return;
unhandled:
        _printf(" ^ \e[33mUnhandled CSI\e[0m\n");
}

void resetcsi(struct frame *f)
{
        memset(&f->csi, 0, sizeof f->csi);
}

void resetesc(struct frame *f)
{
        f->esc = 0;
}

int eschandle(struct frame *f, uint32_t c)
{
        switch (c) {
        case '[':
                f->esc |= ESC_CSI;
                return 0;
        case 'P': /* DCS -- Device Control String */
        case '_': /* APC -- Application Program Command */
        case '^': /* PM -- Privacy Message */
        case ']': /* OSC -- Operating System Command */
        case 'k': /* old title set compatibility */
                tstrsequence(f, c);
                return 0;
        case 'M': /* RI - Reverse index */
                if (f->c.y == f->top) {
                        tscrolldown(f, f->top, 1);
                } else {
                        tmoveto(f, f->c.x, f->c.y - 1);
                }
                return 1;
        case '(': /* GZD4 - Set primary charset G0 */
        case ')': /* G1D4 - Set secondary charset G1 */
        case '*': /* G2D4 - Set tertiary charset G2 */
        case '+': /* G3D4 - Set quaternary charset G3 */
                f->icharset = c - '(';
                f->esc |= ESC_ALTCHARSET;
                return 0;
        case '\\': /* ST - String terminator */
                if (f->esc & ESC_STR_END)
                        tstrhandle(f);
                break;
        default:
                _printf("\e[31mUnhandled escape %c\e[0m\n", (unsigned char)c);
        }

        return 1;
}
