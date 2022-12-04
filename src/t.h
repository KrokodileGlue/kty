#ifndef T_H
#define T_H

#include <stdint.h>
#include <stdbool.h>

struct frame;

int twrite(struct frame *f, const char *buf, int buflen);
void tputc(struct frame *f, uint32_t c);
void tcontrolcode(struct frame *f, uint32_t c);
void tnewline(struct frame *f, int first_col);
void tscrollup(struct frame *f, int orig, int n);
void tscrolldown(struct frame *f, int orig, int n);
void tsetscroll(struct frame *f, int top, int bot);
void tmoveato(struct frame *f, int x, int y);
void tmoveto(struct frame *f, int x, int y);
void tclearregion(struct frame *f, int x0, int y0, int x1, int y1);
void tprintc(struct frame *f, uint32_t c);
void tresize(struct frame *f, int col, int row);
void tdeletechar(struct frame *f, int n);
void tinsertblank(struct frame *f, int n);
void tstrsequence(struct frame *f, unsigned char c);
void tstrhandle(struct frame *f);
void tsetattr(struct frame *f);
void tcursor(struct frame *f, bool save);
void tswapscreen(struct frame *f);
void tcsihandle(struct frame *f);
int teschandle(struct frame *f, uint32_t c);
void handle_terminal_mode(struct frame *f, int set, bool priv);

#endif
