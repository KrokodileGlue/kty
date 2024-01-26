#pragma once

#include "renderer.h"

struct canvas {
        int width, height;
        int cw, ch;
        int col, row;
        int pt_size;
        int ascender;
        unsigned program;
        unsigned framebuffer;
        unsigned texture;
        struct renderer *r;
};

int canvas_init(struct canvas *c, struct renderer *r);
int canvas_resize(struct canvas *c, int width, int height, int pt_size);
int canvas_render_string(struct canvas *c, int x, int y, const char *s);
int canvas_render(struct canvas *c);
