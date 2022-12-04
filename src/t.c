#define _XOPEN_SOURCE 600

#include "t.h"

#include <inttypes.h>                  /* PRIx32 */
#include <stdint.h>                    /* uint32_t */
#include <stdio.h>                     /* fprintf, stderr */
#include <stdlib.h>                    /* realloc, atoi, calloc */
#include <string.h>                    /* memmove, memset */
#include <wchar.h>                     /* wcwidth */

#include "esc.h"                       /* resetcsi, resetesc, csihandle */
#include "font.h"                      /* cell, CELL_BOLD, CELL_DUMMY */
#include "frame.h"                     /* frame, cursor, frame::(anonymous) */
#include "render.h"                    /* font_renderer */
#include "utf8.h"                      /* utf8decode, utf8encode */
#include "util.h"                      /* _printf, ESC_ARG_SIZE, ISCONTROL */

/* TODO: Make this a macro. */
int limit(int *x, int y, int z)
{
        if (*x < y) *x = y;
        if (*x > z) *x = z;
        return *x;
}

/*
 * Swap the saved cursor and the real cursor.
 */
void tcursor(struct frame *f)
{
        struct cursor tmp = f->altcursor;
        f->altcursor = f->c;
        f->c = tmp;
}

void tresize(struct frame *f, int col, int row)
{
        _printf("%d,%d -> %d,%d\n", f->col, f->row, col, row);

        /* Save a little effort. */
        if (f->col == col && f->row == row) return;

        f->line = realloc(f->line, row * sizeof *f->line);
        f->linewrapped = realloc(f->linewrapped, row * sizeof *f->linewrapped);

        limit(&f->c.x, 0, col - 1);
        limit(&f->c.y, 0, row - 1);

        for (int i = f->row; i < row; i++) {
                f->line[i] = calloc(f->col, sizeof *f->line[i]);
                f->linewrapped[i] = 0;
        }

        if (col > f->col)
                for (int i = 0; i < row; i++) {
                        f->line[i] = realloc(f->line[i], col * sizeof *f->line[i]);
                        memset(f->line[i] + f->col, 0, (col - f->col) * sizeof **f->line);
                }

        bool wrapped[row];
        memset(wrapped, 0, sizeof wrapped);

        for (int i = 0; i < f->row; i++)
                _printf("  [%d] = %d\n", i, f->linewrapped[i]);

        if (col > f->col) {
                for (int i = 0; i < row; i++) {
                        _printf("processing expansion at line %d\n", i);
                        struct { int x, y; } a = { 0, i }, b = a;

                        int endofblock = i;

                        while (f->linewrapped[endofblock] && endofblock < row) endofblock++;

                        int numlines = endofblock - i + 1;

                        int lastlineupdated = a.y;

                        while ((b.y && f->linewrapped[b.y - 1]) || (b.y == i && f->linewrapped[i])) {
                                if (b.y >= row || a.y >= row) break;

                                f->line[a.y][a.x] = f->line[b.y][b.x];

                                if (f->line[b.y][b.x].c)
                                        lastlineupdated = a.y;

                                if (a.x != b.x || a.y != b.y)
                                        f->line[b.y][b.x] = (struct cell){ 0 };

                                a.x++;
                                b.x++;

                                if (a.x == col) {
                                        _printf("marking %d as wrapped\n", a.y);
                                        wrapped[a.y] = true;
                                        a.x = 0;
                                        a.y++;
                                }

                                if (b.x == f->col) {
                                        b.x = 0;
                                        b.y++;
                                }

                                if (!((b.y && f->linewrapped[b.y - 1]) || (b.y == i && f->linewrapped[i]))) {
                                        b.y++;
                                        break;
                                }
                        }

                        for (int j = i; j < lastlineupdated; j++) {
                                _printf("wrapping %d\n", j);
                                wrapped[j] = true;
                        }

                        for (int j = lastlineupdated; j <= a.y; j++) {
                                _printf("unwrapping %d\n", j);
                                wrapped[j] = false;
                        }

                        for (int j = 0; j <= a.y; j++) {
                                f->linewrapped[j] = false;
                        }

                        _printf("lastlineupdated = %d, endofblock = %d\n", lastlineupdated, endofblock);
                        for (int j = 0; j < endofblock - lastlineupdated; j++) {
                                _printf("scrolling up %d\n", lastlineupdated + 1);
                                tscrollup(f, lastlineupdated + 1, 1);
                                if (f->c.y > lastlineupdated + 1) f->c.y--;
                        }

                        i = lastlineupdated;
                }
        } else if (col < f->col) {
                /*
                 * If we're shrinking horizontally then lines which
                 * are currently considered wrapped should remain so.
                 */
                /* memcpy(wrapped, f->linewrapped, sizeof wrapped); */

                for (int i = 0; i < row; i++) {
                        int eol = 0;

                        for (int j = f->col - 1; j >= col; j--)
                                if (f->line[i][j].c) {
                                        eol = j;
                                        break;
                                }

                        /* if (eol < col) continue; */

                        struct { int x, y; } a = { 0, i }, b = a;

                        struct cell line[f->col];
                        memcpy(line, f->line[i], sizeof line);

                        _printf("eol: %d\n", eol);
                        if (!f->linewrapped[a.y] && eol >= col) {
                                for (int j = 0; j <= eol; j++) {
                                        f->line[a.y][a.x] = line[b.x];

                                        if (b.y >= 0 && (a.x != b.x || a.y != b.y))
                                                f->line[b.y][b.x] = (struct cell){ 0 };

                                        a.x++;
                                        b.x++;

                                        if (a.x == col) {
                                                tscrolldown(f, a.y + 1, 1);
                                                if (f->c.y >= a.y) f->c.y++;
                                                a.x = 0;
                                                a.y++;
                                                memset(f->line[a.y], 0, col * sizeof *f->line);
                                        }
                                }

                                for (int j = i; j < a.y; j++) {
                                        _printf("single long line %d\n", j);
                                        wrapped[j] = true;
                                }

                                i = a.y;
                                continue;
                        }

                        if (!f->linewrapped[i]) continue;

                        /*
                         * If we reach this point then we're looking
                         * at a wrapped line. The sequence of wrapped
                         * lines may be long.
                         */

                        int endofblock = i;

                        while (f->linewrapped[endofblock] && endofblock < row) endofblock++;

                        _printf("re-wrapping a block of long lines %d-%d\n", i, endofblock - 1);

                        int numlines = endofblock - i + 1;

                        struct cell **block = malloc(numlines * sizeof *block);

                        for (int j = 0; j < numlines; j++) {
                                block[j] = malloc(f->col * sizeof **block);
                                memcpy(block[j], f->line[i + j], f->col * sizeof **block);
                        }

                        while (b.y <= endofblock) {
                                int eol = 0;

                                for (int j = f->col - 1; j >= 0; j--)
                                        if (block[b.y - i][j].c) {
                                                eol = j;
                                                break;
                                        }

                                if (b.y == endofblock && b.x > eol) break;

                                f->line[a.y][a.x] = block[b.y - i][b.x];

                                a.x++;
                                b.x++;

                                if (a.x == col) {
                                        a.x = 0;
                                        a.y++;

                                        if (a.y >= endofblock + 1) {
                                                tscrolldown(f, a.y, 1);
                                                memset(f->line[a.y], 0, col * sizeof *f->line);
                                                if (f->c.y >= a.y) f->c.y++;
                                        }
                                }

                                if (b.x == f->col) {
                                        b.x = 0;
                                        b.y++;
                                }
                        }

                        _printf("re-wrapping wrapped[%d-%d]\n", i, a.y - 1);
                        for (int j = i; j < a.y; j++)
                                wrapped[j] = true;

                        i = a.y - 1;
                }
        }

        memcpy(f->linewrapped, wrapped, sizeof wrapped);

        f->col = col;
        f->row = row;
        f->font->dirty_display = 1;
}

void tprintc(struct frame *f, uint32_t c)
{
        int mode = f->c.mode | (wcwidth(c) == 2 ? CELL_WIDE : 0);

        if (f->c.x >= f->col) {
                f->linewrapped[f->c.y] = true;
                _printf("wrapping %d\n", f->c.y);
                mode |= CELL_WRAP;
                f->c.x = 0;
                f->c.y++;
        }

        /*
         * TODO: This might be more appropriate in `tputc`.
         */
        if (f->c.y >= f->row) {
                int diff = f->c.y - f->row;
                f->c.y -= diff;
                if (f->c.y == f->row) {
                        f->c.y--;
                        diff--;
                }
                tscrollup(f, 0, abs(diff));
        }

        f->line[f->c.y][f->c.x++] = (struct cell){
                .c = c,
                .mode = mode,
                .fg = f->c.fg,
                .bg = f->c.bg,
        };

        if (wcwidth(c) > 1) {
                int wrapped = 0;

                if (f->c.x >= f->col) {
                        f->linewrapped[f->c.y] = true;
                        wrapped = CELL_WRAP;
                        f->c.x = 0;
                        f->c.y++;
                }

                if (f->c.y >= f->row) {
                        int diff = f->c.y - f->row;
                        f->c.y -= diff;
                        if (f->c.y == f->row) {
                                f->c.y--;
                                diff--;
                        }
                        tscrollup(f, 0, abs(diff));
                }

                f->line[f->c.y][f->c.x++] = (struct cell){
                        .mode = CELL_DUMMY | wrapped,
                };
        }

        if (f->c.x % f->col == 0)
                f->c.mode |= CELL_WRAPNEXT;

        f->font->dirty_display = 1;
}

void tinsertblank(struct frame *f, int n)
{
        int dst, src, size;
        struct cell *line;

        limit(&n, 0, f->col - f->c.x);

        dst = f->c.x + n;
        src = f->c.x;
        size = f->col - dst;
        line = f->line[f->c.y];

        memmove(&line[dst], &line[src], size * sizeof(struct cell));
        tclearregion(f, src, f->c.y, dst - 1, f->c.y);
}

void tclearregion(struct frame *f, int x0, int y0, int x1, int y1)
{
        _printf("Clearing region %d,%d,%d,%d\n", x0, y0, x1, y1);

        if (x1 >= f->col) x1 = f->col - 1;
        if (y1 >= f->row) y1 = f->row - 1;

        for (int i = y0; i <= y1; i++) {
                f->linewrapped[i] = false;
                for (int j = x0; j <= x1; j++)
                        f->line[i][j] = (struct cell){
                                .c = 0,
                                .mode = 0,
                                .fg = -1,
                                .bg = -1,
                        };
        }

        f->font->dirty_display = 1;
}

void tmoveto(struct frame *f, int x, int y)
{
        _printf("Moving cursor to %d,%d\n", x, y);

        int miny = 0, maxy = f->row - 1;

        if (f->c.state & CELL_ORIGIN) {
                miny = f->top;
                maxy = f->bot;
        }

        f->c.y = limit(&y, miny, maxy);
        f->c.x = limit(&x, 0, f->col - 1);
        f->font->dirty_display = 1;
}

void tmoveato(struct frame *f, int x, int y)
{
        _printf("Moving cursor to %d,%d\n", x, y);
        tmoveto(f, x, y + ((f->c.state & CELL_ORIGIN) ? f->top : 0));
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
        _printf("Scrolling %d lines around %d\n", n, orig);

        tclearregion(f, 0, f->bot - n + 1, f->col - 1, f->bot);

        for (int i = f->bot; i >= orig + n; i--) {
                struct cell *tmp = f->line[i];
                f->line[i] = f->line[i - n];
                f->line[i - n] = tmp;

                bool temp = f->linewrapped[i];
                f->linewrapped[i] = f->linewrapped[i - n];
                f->linewrapped[i - n] = temp;
        }

        f->font->dirty_display = 1;
}

void tscrollup(struct frame *f, int orig, int n)
{
        _printf("Scrolling %d lines around %d\n", n, orig);

        tclearregion(f, 0, orig, f->col - 1, orig + n - 1);

        for (int i = orig; i <= f->bot - n; i++) {
                struct cell *tmp = f->line[i];
                f->line[i] = f->line[i + n];
                f->line[i + n] = tmp;

                bool temp = f->linewrapped[i];
                f->linewrapped[i] = f->linewrapped[i + n];
                f->linewrapped[i + n] = temp;
        }

        f->font->dirty_display = 1;
}

void tnewline(struct frame *f, int first_col)
{
        _printf("tnewline\n");

        int y = f->c.y;

        if (y == f->bot) {
                tscrollup(f, f->top, 1);
        } else {
                y++;
        }

        tmoveto(f, first_col ? 0 : f->c.x, y);
}

void tstrparse(struct frame *f)
{
        f->esc_str.narg = 0;
        f->esc_str.buf[f->esc_str.len] = 0;

        char *p = f->esc_str.buf;

        if (!*p) return;

        while (f->esc_str.narg < ESC_ARG_SIZE) {
                f->esc_str.arg[f->esc_str.narg++] = p;
                int c;
                while ((c = *p) && c != ';') ++p;
                if (!c) return;
                *p++ = 0;
        }
}

/*
 * Parse and execute string escape sequences.
 */
void tstrhandle(struct frame *f)
{
        _printf("\e[33m%.*s\e[39m\n", f->esc_str.len, f->esc_str.buf);

        tstrparse(f);

        int par = f->esc_str.narg ? atoi(f->esc_str.arg[0]) : 0;

        switch (f->esc_str.type) {
        case ']':
                switch (par) {
                case 0:
                        if (f->esc_str.narg > 1)
                                frame_title(f, f->esc_str.arg[1]);
                        break;
                default:
                        _printf("\e[34mUnhandled `[` style string escape sequence\e[39m\n");
                        break;
                }
                break;
        case 'P':              /* DCS - Device control string */
                _printf("\e[34mTODO: DCS - Device control string\e[39m\n");
                break;
        case '_':             /* APC - Application program command */
                _printf("\e[34mTODO: APC - Application program command\e[39m\n");
                break;
        case '^':              /* PM - Privacy message */
                _printf("\e[34mTODO: PM - Privacy message\e[39m\n");
                break;
        case 'k':
                frame_title(f, f->esc_str.arg[0]);
                break;
        default:
                _printf("\e[34mUnhandled string escape sequence with type `%c`\e[39m\n", f->esc_str.type);
                break;
        }

        f->esc_str.len = 0;
        f->esc_str.type = 0;
        f->esc &= ~(ESC_STR | ESC_STR_END);
}

void tcontrolcode(struct frame *f, uint32_t c)
{
        _printf("\e[35m\\x%"PRIx32"\e[39m\n", c);

        switch (c) {
        case ESC:
                resetcsi(f);
                f->esc &= ~(ESC_CSI | ESC_ALTCHARSET);
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
                /* The bell sound is annoying anyway. */
                if (f->esc & ESC_STR_END)
                        tstrhandle(f);
                break;
        case BS:
                tmoveto(f, f->c.x - 1, f->c.y);
                break;
        }
}

void tdeletechar(struct frame *f, int n)
{
        int dst, src, size;
        struct cell *line;

        limit(&n, 0, f->col - f->c.x);

        dst = f->c.x;
        src = f->c.x + n;
        size = f->col - src;
        line = f->line[f->c.y];

        memmove(&line[dst], &line[src], size * sizeof(struct cell));
        tclearregion(f, f->col-n, f->c.y, f->col-1, f->c.y);
}

void tputc(struct frame *f, uint32_t c)
{
        /* Here's the legwork of actually interpreting commands. */

        if (f->esc & ESC_STR) {
                if (c == '\a' || c == 030 || c == 032 || c == 033
                        || ISCONTROLC1(c)) {
                        f->esc &= ~(ESC_START|ESC_STR);
                        f->esc |= ESC_STR_END;
                        goto check_control_code;
                }

                /* TODO: Handle unending string escape sequences. */

                unsigned len;
                unsigned char buf[4];
                utf8encode(c, buf, &len);
                memmove(&f->esc_str.buf[f->esc_str.len], buf, len);
                f->esc_str.len += len;
                return;
        }

 check_control_code:
        if (ISCONTROL(c)) {
                tcontrolcode(f, c);
        } else if (f->esc & ESC_START) {
                if (f->esc & ESC_CSI) {
                        f->csi.buf[f->csi.len++] = c;
                        if ((c >= 0x40 && c <= 0x7E)
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
                } else if (f->esc & ESC_ALTCHARSET) {
                        _printf("TODO: Handle alternate charsets\n");
                } else if (eschandle(f, c))
                        resetesc(f);
        } else {
                tprintc(f, c);
        }
}

void thandlegraphicmode(struct frame *f, long arg)
{
        if (arg >= 30 && arg <= 38) {
                f->c.fg = arg - 30;
                return;
        }

        if (arg >= 40 && arg <= 48) {
                f->c.bg = arg - 40;
                return;
        }

        if (arg >= 90 && arg <= 97) {
                f->c.fg = arg - 90 + 8;
                return;
        }

        if (arg >= 100 && arg <= 107) {
                f->c.bg = arg - 100 + 8;
                return;
        }

        switch (arg) {
        case 0:
                f->c.fg = -1;
                f->c.bg = -1;
                f->c.mode = 0;
                break;
        case 1: /* Bold */
                f->c.mode |= CELL_BOLD;
                break;
        case 22: /* Turn off bold */
                f->c.mode &= ~CELL_BOLD;
                break;
        case 3: /* Italic */
                f->c.mode |= CELL_ITALIC;
                break;
        case 23: /* Turn off italic */
                f->c.mode &= ~CELL_ITALIC;
                break;
        case 4: /* Underline */
                f->c.mode |= CELL_UNDERLINE;
                break;
        case 24: /* Turn off underline */
                f->c.mode &= ~CELL_UNDERLINE;
                break;
        case 7:                 /* Turn on inverse video mode */
                f->c.mode |= CELL_INVERSE;
                break;
        case 27:                /* Turn off inverse video mode */
                f->c.mode &= ~CELL_INVERSE;
                break;
        case 39:
                f->c.fg = -1;
                break;
        case 49:
                f->c.bg = -1;
                break;
        default:
                fprintf(stderr, "Unknown CSI sequence argument %ld\n", arg);
                break;
        }
}

#define TRUECOLOR(r,g,b) ((\
                (((r) & 0xFF) << 16) |\
                (((g) & 0xFF) << 8) |\
                (((b) & 0xFF) << 0)) + 256\
                )

void tsetattr(struct frame *f)
{
        if (!f->csi.narg) {
                thandlegraphicmode(f, 0);
                return;
        }

        if (f->csi.narg == 5 && f->csi.arg[0] == 38 && f->csi.arg[1] == 2) {
                /* This is a truecolor fg sequence. */
                f->c.fg = TRUECOLOR(f->csi.arg[2], f->csi.arg[3], f->csi.arg[4]);
                return;
        }

        if (f->csi.narg == 5 && f->csi.arg[0] == 48 && f->csi.arg[1] == 2) {
                /* This is a truecolor bg sequence. */
                f->c.bg = TRUECOLOR(f->csi.arg[2], f->csi.arg[3], f->csi.arg[4]);
                return;
        }

        if (f->csi.narg == 3 && f->csi.arg[0] == 38 && f->csi.arg[1] == 5) {
                /* This is a 256 fg sequence. */
                f->c.fg = f->csi.arg[2];
                return;
        }

        if (f->csi.narg == 3 && f->csi.arg[0] == 48 && f->csi.arg[1] == 5) {
                /* This is a 256 bg sequence. */
                f->c.bg = f->csi.arg[2];
                return;
        }

        for (int i = 0; i < f->csi.narg; i++)
                thandlegraphicmode(f, f->csi.arg[i]);
}

void tstrsequence(struct frame *f, unsigned char c)
{
        switch (c) {
        case 0x90:              /* DCS - Device control string */
                c = 'P';
                break;
        case 0x9f:             /* APC - Application program command */
                c = '_';
                break;
        case 0x9e:              /* PM - Privacy message */
                c = '^';
                break;
        case 0x9d:              /* OSC - Operating system command */
                c = ']';
                break;
        }

        f->esc_str.type = c;
        f->esc |= ESC_STR;
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
