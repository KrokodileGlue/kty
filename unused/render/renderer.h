#pragma once

#include "layout/layout_engine.h"

struct renderer {
        struct layout_engine *e;
        unsigned program;
};

struct canvas;

int renderer_init(struct renderer *r);
int renderer_begin(struct renderer *r);
int renderer_upload_glyphs(struct renderer *r);
int renderer_render_canvas(struct renderer *r, struct canvas *c);
