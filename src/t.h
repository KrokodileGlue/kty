#ifndef T_H
#define T_H

#include <stdint.h>
#include <stdbool.h>

#include "esc.h"

struct term;
struct grid;

/*
 * There should be only one of these functions available: twrite. The
 * rest should be internal.
 */

void tswapscreen(struct term *t);

int twrite(struct term *f, const char *buf, int buflen);
void tputc(struct term *f, uint32_t c);
void tcontrolcode(struct grid *f, uint32_t c);
void tnewline(struct grid *f, int first_col);
void tscrollup(struct grid *f, int orig, int n);
void tscrolldown(struct grid *f, int orig, int n);
void tsetscroll(struct grid *f, int top, int bot);
void tmoveato(struct grid *f, int x, int y);
void tmoveto(struct grid *f, int x, int y);
void tclearregion(struct grid *f, int x0, int y0, int x1, int y1);
void tprintc(struct grid *f, uint32_t c);
void tresize(struct grid *f, int col, int row);
void tdeletechar(struct grid *f, int n);
void tinsertblank(struct grid *f, int n);
void tstrsequence(struct grid *f, unsigned char c);
void tstrhandle(struct grid *f);
void tsetattr(struct grid *f);
void tcursor(struct grid *f, int save);
void tcsihandle(struct term *f, struct csi *csi);
int teschandle(struct grid *f, uint32_t c);
void handle_gridinal_mode(struct grid *f, int set, bool priv);
void tresetesc(struct grid *g);

#endif
