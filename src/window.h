#ifndef WINDOW_H
#define WINDOW_H

struct global;

struct window {
        char **env;

        struct term *term;
        int nterm;

        enum {
                WINDOW_VERTICAL,
                WINDOW_HORIZONTAL,
        } direction;

        struct window *parent, *left, *right;

        float split;

        struct global *global;

        int x0, y0, x1, y1;
};

void window_init(struct window *w, struct global *g, char **env);
void window_place(struct window *w, int x0, int y0, int x1, int y1);

#endif
