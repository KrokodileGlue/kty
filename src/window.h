#ifndef WINDOW_H
#define WINDOW_H

#include <stdio.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "platform.h"

struct global;
struct font_renderer;

struct window {
        struct wterm {
                pthread_t thread; /* TODO: Move this into platform. */
                struct subprocess *subprocess;

                int font_size;

                /* Input buffer */
                char buf[BUFSIZ];
                int buflen;

                struct term *term;

                struct wterm *next;
        } *wterm;

        enum {
                WINDOW_VERTICAL,
                WINDOW_HORIZONTAL,
        } direction;

        struct window *parent, *left, *right;
        float split;
        int x0, y0, x1, y1;
        int nterm;
};

void window_init(struct window *w);
void window_place(struct window *w, int x0, int y0, int x1, int y1);
void window_spawn(struct window *w);

/*
 * TODO: Move rendering out of individual components and into
 * `render.c`.
 */
void window_render(struct window *w, struct font_renderer *r);

#endif
