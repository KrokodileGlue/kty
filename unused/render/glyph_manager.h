#pragma once

#include <stdint.h>

#include "vec.h"

#define MAX_CODE_POINTS_PER_CELL 10

struct gpu_cell {
        struct vec2 vertices[6];
        struct vec2 texture_coordinates[6];
        struct vec3 fg, bg;
};

struct glyph {
        uint32_t c[MAX_CODE_POINTS_PER_CELL];
        unsigned num_code_point;
        struct vec2 vertices[6];
        int tex;
        struct vec2 texture_coordinates[6];
};

struct glyph_manager;

struct cpu_cell {
        uint32_t c[MAX_CODE_POINTS_PER_CELL];
        unsigned num_code_point;
        int fg, bg;
};

struct glyph_manager *glyph_manager_create(void);
int glyph_manager_init(struct glyph_manager *m);
int glyph_manager_destroy(struct glyph_manager *m);
int glyph_manager_add_font_from_name(struct glyph_manager *m, const char *name);
struct glyph *glyph_manager_generate_glyph(struct glyph_manager *m,
                                           struct cpu_cell *c,
                                           int font_size);
