#define _XOPEN_SOURCE 600

#include <stdint.h>
#include <inttypes.h>
#include <wchar.h>

/* TODO: Remove OpenGL specific stuff from the frame. */
#include <GL/glew.h>
#include <GL/gl.h>

#include "gl.h"
#include "util.h"
#include "frame.h"
#include "t.h"
#include "esc.h"
#include "utf8.h"

/* TODO: Make this a macro. */
int limit(int *x, int y, int z)
{
        if (*x < y) *x = y;
        if (*x > z) *x = z;
        return *x;
}

void tresize(struct frame *f, int col, int row)
{
        _printf("tresize(%d,%d -> %d,%d)\n", f->col, f->row, col, row);

        f->line = realloc(f->line, row * sizeof *f->line);

        for (int i = f->row; i < row; i++) {
                f->line[i] = calloc(f->col, sizeof *f->line[i]);
        }

        for (int i = 0; i < row; i++) {
                f->line[i] = realloc(f->line[i], col * sizeof *f->line[i]);
                if (col > f->col)
                        memset(&f->line[i][f->col], 0, (col - f->col) * sizeof **f->line);
        }

        f->col = col;
        f->row = row;
        f->dirty_display = 1;
}

void tprintc(struct frame *f, uint32_t c)
{
        _printf("tprintc(U+%x/%c) (%d,%d)\n", c, c, f->c.x, f->c.y);
        f->dirty_display = 1;
        f->line[f->c.y][f->c.x] = (struct glyph){
                .c = c,
                .mode = f->c.mode | (wcwidth(c) == 2 ? GLYPH_WIDE : 0),
                .fg = f->c.fg,
                .bg = f->c.bg,
        };
}

void tclearregion(struct frame *f, int x0, int y0, int x1, int y1)
{
        _printf("tclearregion(%d,%d,%d,%d)\n", x0, y0, x1, y1);

        for (int i = y0; i <= y1; i++)
                for (int j = x0; j <= x1; j++)
                        f->line[i][j] = (struct glyph){
                                .c = 0,
                                .mode = 0,
                                .fg = -1,
                                .bg = -1,
                        };
        f->dirty_display = 1;
}

void tmoveto(struct frame *f, int x, int y)
{
        _printf("tmoveto(%d,%d)\n", x, y);

	int miny, maxy;

	if (f->c.state & CURSOR_ORIGIN) {
		miny = f->top;
		maxy = f->bot;
	} else {
		miny = 0;
		maxy = f->row - 1;
	}

	f->c.state &= ~CURSOR_WRAPNEXT;
	f->c.x = limit(&x, 0, f->col - 1);
	f->c.y = limit(&y, miny, maxy);
        f->dirty_display = 1;
}

void tmoveato(struct frame *f, int x, int y)
{
        _printf("tmoveato(%d,%d)\n", x, y);
	tmoveto(f, x, y + ((f->c.state & CURSOR_ORIGIN) ? f->top: 0));
}

void tsetscroll(struct frame *f, int top, int bot)
{
        limit(&bot, 0, f->row - 1);
        limit(&top, 0, f->row - 1);

        if (top > bot) {
                int tmp = bot;
                bot = top;
                top = tmp;
        }

        f->top = top;
        f->bot = bot;
}

void tscrolldown(struct frame *f, int orig, int n)
{
        tclearregion(f, 0, f->bot - n + 1, f->col - 1, f->bot);

        for (int i = f->bot; i >= orig + n; i--) {
                struct glyph *temp = f->line[i];
                f->line[i] = f->line[i - n];
                f->line[i - n] = temp;
        }

        f->dirty_display = 1;
}

void tscrollup(struct frame *f, int orig, int n)
{
        _printf("tscrollup(%d,%d)\n", orig, n);

        tclearregion(f, 0, orig, f->col - 1, orig + n - 1);

        for (int i = orig; i <= f->bot - n; i++) {
                struct glyph *tmp = f->line[i];
                f->line[i] = f->line[i + n];
                f->line[i + n] = tmp;
        }

        f->dirty_display = 1;
}

void tnewline(struct frame *f, int first_col)
{
        _printf("tnewline(%d)\n", first_col);

        int y = f->c.y;

        if (y == f->bot) {
                tscrollup(f, f->top, 1);
        } else {
                y++;
        }

        tmoveto(f, first_col ? 0 : f->c.x, y);
}

void tcontrolcode(struct frame *f, uint32_t c)
{
        _printf("control code %"PRIX32"\n", c);
        switch (c) {
        case ESC:
                f->esc |= ESC_START;
                break;
        case LF:
        case VT:
                tnewline(f, f->mode & MODE_CRLF);
                break;
        case CR:
                tmoveto(f, 0, f->c.y);
                break;
        case HT:
                tmoveto(f, (f->c.x / 8 + 1) * 8, f->c.y);
                break;
        case BEL:
                break;
        case BS:
                tmoveto(f, f->c.x - 1, f->c.y);
                break;
        }
}

void tdeletechar(struct frame *f, int n)
{
	int dst, src, size;
	struct glyph *line;

	limit(&n, 0, f->col - f->c.x);

	dst = f->c.x;
	src = f->c.x + n;
	size = f->col - src;
	line = f->line[f->c.y];

	memmove(&line[dst], &line[src], size * sizeof(struct glyph));
	tclearregion(f, f->col-n, f->c.y, f->col-1, f->c.y);
}

void tputc(struct frame *f, uint32_t c)
{
        //_printf("tputc(U+%x/%c) (%d,%d)\n", c, c, f->c.x, f->c.y);

        /* TODO: ??? */
        if (f->c.x >= f->col) f->c.x = f->col - 1;

        /* Here's the legwork of actually interpreting commands. */

        if (ISCONTROL(c)) {
                tcontrolcode(f, c);
        } else if (f->esc & ESC_START) {
                if (f->esc & ESC_CSI) {
                        f->csi.buf[f->csi.len++] = c;
                        if ((c > 0x40 && c < 0x7E)
                                || f->csi.len >= sizeof(f->csi.buf) - 1) {
                                f->esc = 0;

                                /*
                                 * So now we have an entire escape sequence in
                                 * `f->esc_buf`, just parse it and execute it.
                                 */
                                csiparse(f);
                                csihandle(f);
                                resetcsi(f);
                                resetesc(f);
                        }
                        return;
                } else {
                        eschandle(f, c);
                }
        } else if (f->c.x < f->col) {
                tprintc(f, c);
                f->c.x += wcwidth(c);
        }
}

int twrite(struct frame *f, const char *buf, int buflen)
{
        int charsize, n;

        for (n = 0; n < buflen; n += charsize) {
                /* TODO: Support commands which alter support for UTF-8. */
                uint32_t c;
                if (!(charsize = utf8decode(buf + n, buflen - n, &c))) break;

                tputc(f, c);
        }

        return n;
}
