#pragma once

#include "layout/layout_engine.h"

struct renderer {
        struct layout_engine *e;
        unsigned program;
};

int renderer_init(struct renderer *r);
int renderer_render_text(struct renderer *r, const char *s);
