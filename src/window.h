#pragma once

#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <GL/glew.h>

#include "platform.h"

struct global;
struct font_renderer;

struct window {
        struct wterm {
                struct subprocess *subprocess;

                /* TODO: Make these opaque handles for a graphics API. */
                GLuint framebuffer;
                GLuint tex_color_buffer;

                int cw, ch;
                int width, height;
                int font_size;

                struct term *term;

                struct window *window;
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
void window_change_font_size(struct wterm *wt, int delta);

/*
 * TODO: Move rendering out of individual components and into
 * `render.c`.
 */
void window_render(struct window *w, struct font_renderer *r);
