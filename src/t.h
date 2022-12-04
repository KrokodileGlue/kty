#ifndef T_H
#define T_H

#include <stdint.h>
#include <stdbool.h>

struct term;

int twrite(struct term *f, const char *buf, int buflen);
void tputc(struct term *f, uint32_t c);
void tcontrolcode(struct term *f, uint32_t c);
void tnewline(struct term *f, int first_col);
void tscrollup(struct term *f, int orig, int n);
void tscrolldown(struct term *f, int orig, int n);
void tsetscroll(struct term *f, int top, int bot);
void tmoveato(struct term *f, int x, int y);
void tmoveto(struct term *f, int x, int y);
void tclearregion(struct term *f, int x0, int y0, int x1, int y1);
void tprintc(struct term *f, uint32_t c);
void tresize(struct term *f, int col, int row);
void tdeletechar(struct term *f, int n);
void tinsertblank(struct term *f, int n);
void tstrsequence(struct term *f, unsigned char c);
void tstrhandle(struct term *f);
void tsetattr(struct term *f);
void tcursor(struct term *f, bool save);
void tswapscreen(struct term *f);
void tcsihandle(struct term *f);
int teschandle(struct term *f, uint32_t c);
void handle_terminal_mode(struct term *f, int set, bool priv);

#endif
