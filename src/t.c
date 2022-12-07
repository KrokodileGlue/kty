#define _XOPEN_SOURCE 600

#include "t.h"

#include <inttypes.h>                  /* PRIx32 */
#include <stdint.h>                    /* uint32_t */
#include <stdio.h>                     /* fprintf, stderr */
#include <stdlib.h>                    /* realloc, atoi, calloc */
#include <string.h>                    /* memmove, memset */
#include <wchar.h>                     /* wcwidth */
#include <unistd.h>

#include "esc.h"                       /* resetcsi, resetesc, csihandle */
#include "font.h"                      /* cell, CELL_BOLD, CELL_DUMMY */
#include "term.h"                      /* term, cursor, term::(anonymous) */
#include "render.h"                    /* font_renderer */
#include "utf8.h"                      /* utf8decode, utf8encode */
#include "util.h"                      /* _printf, ESC_ARG_SIZE, ISCONTROL */
#include "platform.h"

void tswapscreen(struct term *t);
int twrite(struct term *t, const char *buf, int buflen);
void tputc(struct term *t, uint32_t c);
void tcontrolcode(struct term *t, uint32_t c);
void tnewline(struct term *t, int first_col);
void tscrollup(struct term *t, int orig, int n);
void tscrolldown(struct term *t, int orig, int n);
void tsetscroll(struct term *t, int top, int bot);
void tmoveato(struct term *t, int x, int y);
void tmoveto(struct term *t, int x, int y);
void tclearregion(struct term *t, int x0, int y0, int x1, int y1);
void tprintc(struct term *t, uint32_t c);
void tresize(struct term *t, int col, int row);
void tdeletechar(struct term *t, int n);
void tinsertblank(struct term *t, int n);
void tstrsequence(struct term *t, unsigned char c);
void tstrhandle(struct term *t);
void tsetattr(struct term *t);
void tcursor(struct term *t, int save);
void tcsihandle(struct term *t, struct csi *csi);
int teschandle(struct term *t, uint32_t c);
void handle_gridinal_mode(struct term *t, int set, bool priv);
void tresetesc(struct term *t);

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

#define LIMIT(x,y,z) ((x) < (y) ? (y) : ((x) > (z) ? (z) : (x)))

/*
 * Save or load the cursor.
 */
void tcursor(struct term *t, int save)
{
        _printf("%s cursor\n", save ? "Saving" : "Loading");
        if (save) t->cursor[1] = *t->c;
        else *t->c = t->cursor[1];
}

/*
 * Grow the buffers representing the grid to accomodate the new
 * dimensions `col` x `row`. It doesn't update the cursor position or
 * anything else.
 */
static void tgrow(struct term *t, int col, int row)
{
        struct grid *g = t->g;

        if (row > g->row) {
                g->line = realloc(g->line, row * sizeof *g->line);
                g->attr = realloc(g->attr, row * sizeof *g->attr);
                g->wrap = realloc(g->wrap, row * sizeof *g->wrap);

                /*
                 * The reason we use `max(col, g->col)` here is to
                 * ensure that all lines have _at least_ enough for
                 * the original size (i.e. never shrinking a line
                 * buffer) but also being big enough to accomodate
                 * `col` columns.
                 *
                 * If we were to just allocate `col` then horizontally
                 * shrinking would truncate the lines, destroying the
                 * information needed to wrap the text. Just
                 * allocating `g->col` wouldn't be enough if
                 * horizontally growing.
                 *
                 * Of course that's bullshit because these lines are
                 * being added to the end after vertical growth, but
                 * basically it would require special cases in `twrap`
                 * otherwise. `twrap` assumes that all lines can be
                 * read to the original width. So just close your eyes
                 * and imagine that the two paragraphs above are true
                 * for this case and it makes sense; `twrap` doesn't
                 * know they're not true.
                 */

#define max(x,y) (x > y ? x : y)

                for (int i = g->row; i < row; i++) {
                        g->line[i] = calloc(max(col, g->col), sizeof **g->line);
                        g->attr[i] = calloc(max(col, g->col), sizeof **g->attr);
                        g->wrap[i] = 0;
                }
        }

        if (col > g->col)
                for (int i = 0; i < row; i++) {
                        g->line[i] = realloc(g->line[i], col * sizeof **g->line);
                        memset(g->line[i] + g->col, 0, (col - g->col) * sizeof **g->line);

                        g->attr[i] = realloc(g->attr[i], col * sizeof **g->attr);
                        memset(g->attr[i] + g->col, 0, (col - g->col) * sizeof **g->attr);
                }
}

static void twrap(struct term *t, int col, int row)
{
        struct grid *g = t->g;

        bool wrapped[row];
        memset(wrapped, 0, sizeof wrapped);

        if (col > g->col) {
                for (int i = 0; i < row; i++) {
                        struct { int x, y; } a = { 0, i }, b = a;

                        int endofblock = i;

                        while (g->wrap[endofblock] && endofblock < row) endofblock++;

                        int lastlineupdated = a.y;

                        while ((b.y && g->wrap[b.y - 1]) || (b.y == i && g->wrap[i])) {
                                if (b.y >= row || a.y >= row) break;

                                g->line[a.y][a.x] = g->line[b.y][b.x];
                                g->attr[a.y][a.x] = g->attr[b.y][b.x];

                                if (g->line[b.y][b.x])
                                        lastlineupdated = a.y;

                                if (a.x != b.x || a.y != b.y) {
                                        g->line[b.y][b.x] = 0;
                                        g->attr[b.y][b.x] = (struct cell_attr){ 0 };
                                }

                                a.x++;
                                b.x++;

                                if (a.x == col) {
                                        wrapped[a.y] = true;
                                        a.x = 0;
                                        a.y++;
                                }

                                if (b.x == g->col) {
                                        b.x = 0;
                                        b.y++;
                                }

                                if (!((b.y && g->wrap[b.y - 1]) || (b.y == i && g->wrap[i]))) {
                                        b.y++;
                                        break;
                                }
                        }

                        for (int j = i; j < lastlineupdated; j++)
                                wrapped[j] = true;

                        for (int j = lastlineupdated; j <= a.y; j++)
                                wrapped[j] = false;

                        for (int j = 0; j <= a.y; j++) {
                                g->wrap[j] = false;
                        }

                        for (int j = 0; j < endofblock - lastlineupdated; j++) {
                                tscrollup(t, lastlineupdated + 1, 1);
                                if (t->c->y > lastlineupdated + 1) t->c->y--;
                        }

                        i = lastlineupdated;
                }
        } else if (col < g->col) {
                /*
                 * If we're shrinking horizontally then lines which
                 * are currently considered wrapped should remain so.
                 */
                /* memcpy(wrapped, g->wrap, sizeof wrapped); */

                for (int i = 0; i < row; i++) {
                        int eol = 0;

                        for (int j = g->col - 1; j >= col; j--)
                                if (g->line[i][j]) {
                                        eol = j;
                                        break;
                                }

                        /* if (eol < col) continue; */

                        struct { int x, y; } a = { 0, i }, b = a;

                        uint32_t line[g->col];
                        struct cell_attr attr[g->col];
                        memcpy(line, g->line[i], sizeof line);
                        memcpy(attr, g->attr[i], sizeof attr);

                        if (!g->wrap[a.y] && eol >= col) {
                                for (int j = 0; j <= eol; j++) {
                                        g->line[a.y][a.x] = line[b.x];
                                        g->attr[a.y][a.x] = attr[b.x];

                                        if (b.y >= 0 && (a.x != b.x || a.y != b.y)) {
                                                g->line[b.y][b.x] = 0;
                                                g->attr[b.y][b.x] = (struct cell_attr){ 0 };
                                        }

                                        a.x++;
                                        b.x++;

                                        if (a.x == col) {
                                                tscrolldown(t, a.y + 1, 1);
                                                if (t->c->y >= a.y) t->c->y++;
                                                a.x = 0;
                                                a.y++;
                                                if (a.y >= row) break;
                                                memset(g->line[a.y], 0, col * sizeof **g->line);
                                                memset(g->attr[a.y], 0, col * sizeof **g->attr);
                                        }
                                }

                                for (int j = i; j < a.y; j++)
                                        wrapped[j] = true;

                                i = a.y;
                                continue;
                        }

                        if (!g->wrap[i]) continue;

                        /*
                         * If we reach this point then we're looking
                         * at a wrapped line. The sequence of wrapped
                         * lines may be long.
                         */

                        int endofblock = i;

                        while (g->wrap[endofblock] && endofblock < row) endofblock++;

                        int numlines = endofblock - i + 1;

                        uint32_t **block = malloc(numlines * sizeof *block);
                        struct cell_attr **attrblock = malloc(numlines * sizeof *attrblock);

                        for (int j = 0; j < numlines; j++) {
                                block[j] = malloc(g->col * sizeof **block);
                                memcpy(block[j], g->line[i + j], g->col * sizeof **block);

                                attrblock[j] = malloc(g->col * sizeof **attrblock);
                                memcpy(attrblock[j], g->attr[i + j], g->col * sizeof **attrblock);
                        }

                        while (b.y <= endofblock && a.y < row && b.y < row) {
                                int eol = 0;

                                for (int j = g->col - 1; j >= 0; j--)
                                        if (block[b.y - i][j]) {
                                                eol = j;
                                                break;
                                        }

                                if (b.y == endofblock && b.x > eol) break;

                                g->line[a.y][a.x] = block[b.y - i][b.x];
                                g->attr[a.y][a.x] = attrblock[b.y - i][b.x];

                                a.x++;
                                b.x++;

                                if (a.x == col) {
                                        a.x = 0;
                                        a.y++;

                                        if (a.y >= endofblock + 1) {
                                                tscrolldown(t, a.y, 1);
                                                memset(g->line[a.y], 0, col * sizeof **g->line);
                                                memset(g->attr[a.y], 0, col * sizeof **g->attr);
                                                if (t->c->y >= a.y) t->c->y++;
                                        }
                                }

                                if (b.x == g->col) {
                                        b.x = 0;
                                        b.y++;
                                }
                        }

                        for (int j = i; j < a.y; j++)
                                wrapped[j] = true;

                        i = a.y - 1;
                }
        }

        memcpy(g->wrap, wrapped, sizeof wrapped);
}

void tresize(struct term *t, int col, int row)
{
        struct grid *g = t->g;
        if (col <= 0) col = 1;
        if (row <= 0) row = 1;

        _printf("%d,%d -> %d,%d\n", g->col, g->row, col, row);

        /* Save a little effort. */
        if (g->col == col && g->row == row) return;

        t->c->x = LIMIT(t->c->x, 0, col - 1);
        t->c->y = LIMIT(t->c->y, 0, row - 1);

        tgrow(t, col, row);
        twrap(t, col, row);
        g->col = col;
        g->row = row;
        tsetscroll(t, 0, row - 1);

        t->c->x = LIMIT(t->c->x, 0, col - 1);
        t->c->y = LIMIT(t->c->y, 0, row - 1);
}

void tprintc(struct term *t, uint32_t c)
{
        struct grid *g = t->g;
        _printf("U+%"PRIX32"\n", c);
        int mode = t->c->mode | (wcwidth(c) == 2 ? CELL_WIDE : 0);

        if (t->c->x >= g->col) {
                g->wrap[t->c->y] = true;
                _printf("wrapping %d\n", t->c->y);
                mode |= CELL_WRAP;
                t->c->x = 0;
                t->c->y++;
        }

        /*
         * TODO: This might be more appropriate in `tputc`.
         */
        if (t->c->y >= g->row) {
                int diff = t->c->y - g->row;
                t->c->y -= diff;
                if (t->c->y == g->row) {
                        t->c->y--;
                        diff--;
                }
                tscrollup(t, 0, abs(diff));
        }

        g->line[t->c->y][t->c->x] = c;
        g->attr[t->c->y][t->c->x] = (struct cell_attr){
                .mode = mode,
                .fg = t->c->fg,
                .bg = t->c->bg,
        };

        t->c->x++;

        if (wcwidth(c) > 1) {
                int wrapped = 0;

                if (t->c->x >= g->col) {
                        g->wrap[t->c->y] = true;
                        wrapped = CELL_WRAP;
                        t->c->x = 0;
                        t->c->y++;
                }

                if (t->c->y >= g->row) {
                        int diff = t->c->y - g->row;
                        t->c->y -= diff;
                        if (t->c->y == g->row) {
                                t->c->y--;
                                diff--;
                        }
                        tscrollup(t, 0, abs(diff));
                }

                g->line[t->c->y][t->c->x] = 0;
                g->attr[t->c->y][t->c->x] = (struct cell_attr){
                        .mode = CELL_DUMMY | wrapped,
                };

                t->c->x++;
        }

        if (t->c->x % g->col == 0)
                t->c->mode |= CELL_WRAPNEXT;
}

void tinsertblank(struct term *t, int n)
{
        struct grid *g = t->g;
        int dst, src, size;

        uint32_t *line;
        struct cell_attr *attr;

        n = LIMIT(n, 0, g->col - t->c->x);

        dst = t->c->x + n;
        src = t->c->x;
        size = g->col - dst;
        line = g->line[t->c->y];
        attr = g->attr[t->c->y];

        memmove(line + dst, line + src, size * sizeof *line);
        memmove(attr + dst, attr + src, size * sizeof *attr);

        tclearregion(t, src, t->c->y, dst - 1, t->c->y);
}

void tclearregion(struct term *t, int x0, int y0, int x1, int y1)
{
        struct grid *g = t->g;
        _printf("Clearing region %d,%d,%d,%d\n", x0, y0, x1, y1);

        x0 = LIMIT(x0, 0, g->col - 1);
        y0 = LIMIT(y0, 0, g->row - 1);
        x1 = LIMIT(x1, 0, g->col - 1);
        y1 = LIMIT(y1, 0, g->row - 1);

        for (int i = y0; i <= y1; i++) {
                g->wrap[i] = false;
                for (int j = x0; j <= x1; j++) {
                        g->line[i][j] = 0;
                        g->attr[i][j] = (struct cell_attr){
                                .mode = 0,
                                .fg = -1,
                                .bg = -1,
                        };
                }
        }
}

void tmoveto(struct term *t, int x, int y)
{
        struct grid *g = t->g;
        _printf("Moving cursor to %d,%d\n", x, y);

        int miny = 0, maxy = g->row - 1;

        if (t->c->state & CELL_ORIGIN) {
                miny = g->top;
                maxy = g->bot;
        }

        t->c->y = LIMIT(y, miny, maxy);
        t->c->x = LIMIT(x, 0, g->col - 1);
}

void tmoveato(struct term *t, int x, int y)
{
        struct grid *g = t->g;
        _printf("Moving cursor to %d,%d\n", x, y);
        tmoveto(t, x, y + ((t->c->state & CELL_ORIGIN) ? g->top : 0));
}

void tsetscroll(struct term *t, int top, int bot)
{
        struct grid *g = t->g;
        bot = LIMIT(bot, 0, g->row - 1);
        top = LIMIT(top, 0, g->row - 1);

        if (top > bot) {
                int tmp = bot;
                bot = top;
                top = tmp;
        }

        g->top = top;
        g->bot = bot;
}

void tscrolldown(struct term *t, int orig, int n)
{
        struct grid *g = t->g;
        _printf("Scrolling %d lines around %d\n", n, orig);

        tclearregion(t, 0, g->bot - n + 1, g->col - 1, g->bot);

        for (int i = g->bot; i >= orig + n; i--) {
                uint32_t *tmp = g->line[i];
                g->line[i] = g->line[i - n];
                g->line[i - n] = tmp;

                struct cell_attr *tmp2 = g->attr[i];
                g->attr[i] = g->attr[i - n];
                g->attr[i - n] = tmp2;

                bool temp = g->wrap[i];
                g->wrap[i] = g->wrap[i - n];
                g->wrap[i - n] = temp;
        }
}

void tscrollup(struct term *t, int orig, int n)
{
        struct grid *g = t->g;
        _printf("Scrolling %d lines around %d\n", n, orig);

        tclearregion(t, 0, orig, g->col - 1, orig + n - 1);

        for (int i = orig; i <= g->bot - n; i++) {
                uint32_t *tmp = g->line[i];
                g->line[i] = g->line[i + n];
                g->line[i + n] = tmp;

                struct cell_attr *tmp2 = g->attr[i];
                g->attr[i] = g->attr[i + n];
                g->attr[i + n] = tmp2;

                bool temp = g->wrap[i];
                g->wrap[i] = g->wrap[i + n];
                g->wrap[i + n] = temp;
        }
}

void tnewline(struct term *t, int first_col)
{
        struct grid *g = t->g;
        _printf("tnewline\n");

        int y = t->c->y;

        if (y == g->bot) {
                tscrollup(t, g->top, 1);
        } else {
                y++;
        }

        tmoveto(t, first_col ? 0 : t->c->x, y);
}

void tstrparse(struct term *t)
{
        struct grid *g = t->g;
        g->stresc.narg = 0;
        g->stresc.buf[g->stresc.len] = 0;

        char *p = g->stresc.buf;

        if (!*p) return;

        while (g->stresc.narg < ESC_ARG_SIZE) {
                g->stresc.arg[g->stresc.narg++] = p;
                int c;
                while ((c = *p) && c != ';') ++p;
                if (!c) return;
                *p++ = 0;
        }
}

/*
 * Parse and execute string escape sequences.
 */
void tstrhandle(struct term *t)
{
        struct grid *g = t->g;
        _printf("\e[33m%.*s\e[39m\n", g->stresc.len, g->stresc.buf);

        tstrparse(t);

        int par = g->stresc.narg ? atoi(g->stresc.arg[0]) : 0;

        switch (g->stresc.type) {
        case ']':
                switch (par) {
                case 0:
                        /* TODO */
                        /* if (g->stresc.narg > 1) */
                        /*         term_title(t, g->stresc.arg[1]); */
                        break;
                default:
                        _printf("\e[34mUnhandled `]` style string escape sequence\e[39m\n");
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
                /* TODO */
                /* term_title(t, g->stresc.arg[0]); */
                break;
        default:
                _printf("\e[34mUnhandled string escape sequence with type `%c`\e[39m\n", g->stresc.type);
                break;
        }

        g->stresc.len = 0;
        g->stresc.type = 0;
        g->esc &= ~(ESC_STR | ESC_STR_END);
}

void tcontrolcode(struct term *t, uint32_t c)
{
        struct grid *g = t->g;
        _printf("\e[35m\\x%"PRIx32"\e[39m\n", c);

        switch (c) {
        case ESC:
                resetcsi(&g->csi);
                g->esc &= ~(ESC_CSI | ESC_ALTCHARSET);
                g->esc |= ESC_START;
                break;
        case LF:
        case VT:
                /* Should the mode be in the grid? */
                /* TODO: tnewline(t, t->mode & MODE_CRLF); */
                tnewline(t, 0);
                break;
        case CR:
                tmoveto(t, 0, t->c->y);
                break;
        case HT:
                tmoveto(t, (t->c->x / 8 + 1) * 8, t->c->y);
                break;
        case BEL:
                /* The bell sound is annoying anyway. */
                if (g->esc & ESC_STR_END)
                        tstrhandle(t);
                break;
        case BS:
                tmoveto(t, t->c->x - 1, t->c->y);
                break;
        }
}

void tdeletechar(struct term *t, int n)
{
        struct grid *g = t->g;
        int dst, src, size;

        uint32_t *line;
        struct cell_attr *attr;

        n = LIMIT(n, 0, g->col - t->c->x);

        dst = t->c->x;
        src = t->c->x + n;
        size = g->col - src;
        line = g->line[t->c->y];
        attr = g->attr[t->c->y];

        memmove(&line[dst], &line[src], size * sizeof *line);
        memmove(&attr[dst], &attr[src], size * sizeof *attr);

        tclearregion(t, g->col - n, t->c->y, g->col - 1, t->c->y);
}

void tputc(struct term *t, uint32_t c)
{
        /* Here's the legwork of actually interpreting commands. */
        struct grid *g = t->g;

        if (g->esc & ESC_STR) {
                if (c == '\a' || c == 030 || c == 032 || c == 033
                        || ISCONTROLC1(c)) {
                        g->esc &= ~(ESC_START|ESC_STR);
                        g->esc |= ESC_STR_END;
                        goto check_control_code;
                }

                /* TODO: Handle unending string escape sequences. */

                unsigned len;
                unsigned char buf[4];
                utf8encode(c, buf, &len);
                memmove(&g->stresc.buf[g->stresc.len], buf, len);
                g->stresc.len += len;
                return;
        }

 check_control_code:
        if (ISCONTROL(c)) {
                tcontrolcode(t, c);
        } else if (g->esc & ESC_START) {
                if (g->esc & ESC_CSI) {
                        g->csi.buf[g->csi.len++] = c;
                        if ((c >= 0x40 && c <= 0x7E)
                                || g->csi.len >= sizeof(g->csi.buf) - 1) {
                                g->esc = 0;

                                /*
                                 * So now we have an entire escape sequence in
                                 * `g->esc_buf`, just parse it and execute it.
                                 */
                                csiparse(&g->csi);
                                tcsihandle(t, &g->csi);
                                resetcsi(&g->csi);
                                tresetesc(t);
                        }
                        return;
                } else if (g->esc & ESC_ALTCHARSET) {
                        _printf("TODO: Handle alternate charsets\n");
                } else if (teschandle(t, c))
                        tresetesc(t);
        } else {
                tprintc(t, c);
        }
}

void thandlegraphicmode(struct term *t, long arg)
{
        if (arg >= 30 && arg <= 38) {
                t->c->fg = arg - 30;
                return;
        }

        if (arg >= 40 && arg <= 48) {
                t->c->bg = arg - 40;
                return;
        }

        if (arg >= 90 && arg <= 97) {
                t->c->fg = arg - 90 + 8;
                return;
        }

        if (arg >= 100 && arg <= 107) {
                t->c->bg = arg - 100 + 8;
                return;
        }

        switch (arg) {
        case 0:
                t->c->fg = -1;
                t->c->bg = -1;
                t->c->mode = 0;
                break;
        case 1: /* Bold */
                t->c->mode |= CELL_BOLD;
                break;
        case 22: /* Turn off bold */
                t->c->mode &= ~CELL_BOLD;
                break;
        case 3: /* Italic */
                t->c->mode |= CELL_ITALIC;
                break;
        case 23: /* Turn off italic */
                t->c->mode &= ~CELL_ITALIC;
                break;
        case 4: /* Underline */
                t->c->mode |= CELL_UNDERLINE;
                break;
        case 24: /* Turn off underline */
                t->c->mode &= ~CELL_UNDERLINE;
                break;
        case 7:                 /* Turn on inverse video mode */
                t->c->mode |= CELL_INVERSE;
                break;
        case 27:                /* Turn off inverse video mode */
                t->c->mode &= ~CELL_INVERSE;
                break;
        case 39:
                t->c->fg = -1;
                break;
        case 49:
                t->c->bg = -1;
                break;
        default:
                fprintf(stderr, "Unknown CSI sequence argument %ld\n", arg);
                break;
        }
}

void tswapscreen(struct term *t)
{
        _printf("Swapping to %s\n",
                t->mode & MODE_ALTSCREEN ? "primary" : "alternate");
        int col = t->g->col, row = t->g->row;

        /* Change the primary grid pointer. */
        if (t->g == t->grid) t->g = t->grid + 1;
        else t->g = t->grid;

        if (col != t->g->col || row != t->g->row)
                tresize(t, col, row);

        t->mode ^= MODE_ALTSCREEN;
}

#define TRUECOLOR(r,g,b) ((\
                (((r) & 0xFF) << 16) |\
                (((g) & 0xFF) << 8) |\
                (((b) & 0xFF) << 0)) + 256\
                )

void tsetattr(struct term *t)
{
        struct grid *g = t->g;
        if (!g->csi.narg) {
                thandlegraphicmode(t, 0);
                return;
        }

        if (g->csi.narg == 5 && g->csi.arg[0] == 38 && g->csi.arg[1] == 2) {
                /* This is a truecolor fg sequence. */
                t->c->fg = TRUECOLOR(g->csi.arg[2], g->csi.arg[3], g->csi.arg[4]);
                return;
        }

        if (g->csi.narg == 5 && g->csi.arg[0] == 48 && g->csi.arg[1] == 2) {
                /* This is a truecolor bg sequence. */
                t->c->bg = TRUECOLOR(g->csi.arg[2], g->csi.arg[3], g->csi.arg[4]);
                return;
        }

        if (g->csi.narg == 3 && g->csi.arg[0] == 38 && g->csi.arg[1] == 5) {
                /* This is a 256 fg sequence. */
                t->c->fg = g->csi.arg[2];
                return;
        }

        if (g->csi.narg == 3 && g->csi.arg[0] == 48 && g->csi.arg[1] == 5) {
                /* This is a 256 bg sequence. */
                t->c->bg = g->csi.arg[2];
                return;
        }

        for (int i = 0; i < g->csi.narg; i++)
                thandlegraphicmode(t, g->csi.arg[i]);
}

void tstrsequence(struct term *t, unsigned char c)
{
        struct grid *g = t->g;
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

        g->stresc.type = c;
        g->esc |= ESC_STR;
}

int twrite(struct term *t, const char *buf, int buflen)
{
        int charsize, n;

        for (n = 0; n < buflen; n += charsize) {
                /* TODO: Support commands which alter support for UTF-8. */
                uint32_t c;
                if (!(charsize = utf8decode(buf + n, buflen - n, &c))) break;
                tputc(t, c);
        }

        return n;
}

/*
 * Handles the 'h' escape code.
 */
void handle_terminal_mode(struct term *t, int set, bool priv)
{
        int mode = 0;

        if (priv) {
                switch (t->g->csi.arg[0]) {
                case 1:                 /* DECCKM - Cursor key */
                        mode |= MODE_APPCURSOR;
                        break;
                case 7:                 /* DECAWM - Autowrap Mode */
                        mode |= MODE_WRAP;
                        break;
                case 1049: /* Swap screen and cursor */
                        tcursor(t, set);
                        /* FALLTHROUGH */
                case 47: /* swap screen */
                case 1047:
                        if (t->mode & MODE_ALTSCREEN)
                                tclearregion(t, 0, 0, t->g->col - 1, t->g->row - 1);
                        if (set ^ !!(t->mode & MODE_ALTSCREEN))
                                tswapscreen(t);
                        if (t->g->csi.arg[0] != 1049)
                                break;
                        /* FALLTHROUGH */
                case 1048:
                        tcursor(t, set);
                        break;
                case 25: /* Make cursor visible */
                        mode |= MODE_CURSOR_VISIBLE;
                        break;
                }
        }

        if (set) t->mode |= mode;
        else t->mode &= ~mode;
}

#define CSIDEFAULT(x,y) (g->csi.narg ? (x) : (y))

void tcsihandle(struct term *t, struct csi *csi)
{
        struct grid *g = t->g;

        switch (*csi->mode) {
        case '@': /* ICH -- Insert <n> blank char */
                tinsertblank(t, csi->narg ? csi->arg[0] : 1);
                break;
        case 'A': /* Move cursor up n lines */
                tmoveto(t, t->c->x, t->c->y - CSIDEFAULT(csi->arg[0], 1));
                break;
        case 'B': /* Move cursor down n lines */
                tmoveto(t, t->c->x, t->c->y + CSIDEFAULT(csi->arg[0], 1));
                break;
        case 'C': /* Move cursor right n columns */
                tmoveto(t, t->c->x + CSIDEFAULT(csi->arg[0], 1), t->c->y);
                break;
        /* case 'c': */
        /*         platform_write(g->subprocess, VT_IDENTITY, strlen(VT_IDENTITY)); */
        /*         break; */
        case 'D': /* Move cursor left n columns */
                tmoveto(t, t->c->x - CSIDEFAULT(csi->arg[0], 1), t->c->y);
                break;
        case 'h': /* Set terminal mode */
                handle_terminal_mode(t, 1, csi->priv);
                break;
        case 'H': /* CUP - Move cursor too coordinates */
                if (!csi->narg)
                        tmoveto(t, 0, 0);
                else
                        tmoveto(t, csi->arg[1] - 1, csi->arg[0] - 1);
                break;
        case 'J': /* Clear screen */
                switch (csi->arg[0]) {
                case 0: /* below */
                        tclearregion(t, t->c->x, t->c->y, g->col - 1, t->c->y);
                        if (t->c->y < g->row - 1) {
                                tclearregion(t, 0, t->c->y + 1, g->col - 1, g->row - 1);
                        }
                        break;
                case 1: /* above */
                        if (t->c->y > 1)
                                tclearregion(t, 0, 0, g->col - 1, t->c->y - 1);
                        tclearregion(t, 0, t->c->y, t->c->x, t->c->y);
                        break;
                case 2: /* all */
                        tclearregion(t, 0, 0, g->col - 1, g->row - 1);
                        break;
                default:
                        _printf("Unknown clear argument %ld\n", csi->arg[0]);
                        break;
                }
                break;
        case 'K': /* EL - Clear line */
                if (!csi->narg || csi->arg[0] == 0)
                        tclearregion(t, t->c->x, t->c->y, g->col - 1, t->c->y);
                else if (csi->arg[0] == 1)
                        tclearregion(t, 0, t->c->y, t->c->x, t->c->y);
                else if (csi->arg[0] == 2)
                        tclearregion(t, 0, t->c->y, g->col - 1, t->c->y);
                break;
        case 'l': /* Reset terminal mode */
                handle_terminal_mode(t, 0, csi->priv);
                break;
        case 'L': /* IL - Insert n blank lines */
                tscrolldown(t, t->c->y, csi->narg ? csi->arg[0] : 1);
                break;
        case 'm':
                tsetattr(t);
                break;
        case 'M': /* DL - Delete n lines */
                tscrollup(t, t->c->y, csi->narg ? csi->arg[0] : 1);
                break;
        case 'P': /* DCH - Delete n chars */
                tdeletechar(t, csi->narg ? csi->arg[0] : 1);
                break;
	case 'S': /* SU - Scroll n line up */
		tscrollup(t, g->top, csi->narg ? csi->arg[0] : 1);
		break;
	case 's': /* DECSC - Save cursor position */
	case 'u': /* DECRC - Restore cursor position */
		tcursor(t, *csi->mode == 's');
		break;
	case 'T': /* SD - Scroll n line down */
		tscrolldown(t, g->top, csi->narg ? csi->arg[0] : 1);
		break;
        case 'r': /* DECSTBM - Set scroll region */
                if (!csi->narg) tsetscroll(t, 0, g->row - 1);
                else tsetscroll(t, csi->arg[0] - 1, csi->arg[1] - 1);
                tmoveato(t, 0, 0);
                break;
        case 'd': /* VPA - Move to <row> */
                tmoveato(t, t->c->x, (csi->narg ? csi->arg[0] : 1) - 1);
                break;
        case 'G': /* CHA - Move to <col> */
        case '`': /* HPA */
                tmoveato(t, (csi->narg ? csi->arg[0] : 1) - 1, t->c->y);
                break;
        case 'X': /* ECH - Erase n chars */
                tclearregion(t, t->c->x, t->c->y,
                             t->c->x + (csi->narg ? csi->arg[0] : 1), t->c->y);
                break;
        case ' ':
                if (csi->arg[0] < 0 || csi->arg[0] > CURSOR_STYLE_MAX)
                        goto unhandled;
                if (csi->mode[1] == 'q') {
                        t->c->style = csi->arg[0];
                        break;
                }
        default:
                goto unhandled;
        }

        return;
unhandled:
        _printf(" ^ \e[33mUnhandled CSI\e[0m\n");
}

int teschandle(struct term *t, uint32_t c)
{
        struct grid *g = t->g;
        switch (c) {
        case '[':
                g->esc |= ESC_CSI;
                return 0;
        case 'P': /* DCS - Device Control String */
        case '_': /* APC - Application Program Command */
        case '^': /* PM - Privacy Message */
        case ']': /* OSC - Operating System Command */
        case 'k': /* I think this is an xterm thing */
                tstrsequence(t, c);
                return 0;
        case 'M': /* RI - Reverse index */
                if (t->c->y == g->top) {
                        tscrolldown(t, g->top, 1);
                } else {
                        tmoveto(t, t->c->x, t->c->y - 1);
                }
                return 1;
        case '(': /* GZD4 - Set primary charset G0 */
        case ')': /* G1D4 - Set secondary charset G1 */
        case '*': /* G2D4 - Set tertiary charset G2 */
        case '+': /* G3D4 - Set quaternary charset G3 */
                g->charset = c - '(';
                g->esc |= ESC_ALTCHARSET;
                return 0;
        case '\\': /* ST - String terminator */
                if (g->esc & ESC_STR_END)
                        tstrhandle(t);
                break;
        default:
                _printf("\e[31mUnhandled escape %c\e[0m\n", (unsigned char)c);
        }

        return 1;
}

void tresetesc(struct term *t)
{
        struct grid *g = t->g;
        g->esc = 0;
}
