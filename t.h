#ifndef T_H
#define T_H

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

#endif
