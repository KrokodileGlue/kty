// SPDX-License-Identifier: GPL-2.0-only

/*
 * glyph_manager.h
 * Copyright (C) 2023 Taylor West
 *
 * This file contains data structure and function declarations
 * exposing a public API for the management of glyph data.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <cairo/cairo.h>
#include <cairo/cairo-ft.h>

#include "config.h"
#include "cpu_cell.h"
#include "vec.h"

/*
 * Some notes on vocabulary:
 *
 * A "glyph" is a collection of information related to the display of
 * a single renderable thing from a font at a specific size. A glyph
 * contains:
 *
 * + The position and dimensions of the bounding box (this is used for
 *   positioning during rendering)
 * + The id of the glyph sheet where the glyph's sprite may be found
 * + The coordinates of the sprite in the sheet
 *
 * A "sprite" is really just the binary alpha/RGBA data which you can
 * actually use to display the glyph, as its stored in a specific
 * sprite sheet.
 *
 * In general it would be inconvenient if you just want to ask for the
 * data associated with a glyph to get back a giant sprite sheet and
 * have to extract the data using the sprite coordinates (stored in
 * the glyph). It actually works well for GPU rendering because our
 * rendering strategy is to upload all the sprite sheets and glyph
 * data and do all the work in the shader.
 */

struct glyph_sheet {
        int id;                 /* The glyph sheet id */

        int width, height;      /* Width and height in pixels */
        unsigned char *data;    /* The actual texture data */

        enum {
                GLYPH_SHEET_ALPHA,
                GLYPH_SHEET_COLOR
        } format;
};

struct glyph {
        int id;                 /* The id of this glyph */
        int index;              /* The index of this glyph */
        int glyph_sheet;        /* The id of the glyph sheet */

        int pt_size;

        bool bold;
        bool italic;

        struct font *font;

        int bitmap_left;
        int bitmap_top;

        int ascender;

        struct ivec2 size;
        struct ivec2 sprite_coordinates[2];
};

struct glyph_manager {
        /* Cairo sprite map for each font */
        struct sprite_map {
                /*
                 * This surface is the CPU texture object from which
                 * we can extract information to send to the GPU.
                 */
                cairo_surface_t *cairo_surface;

                /*
                 * For regular alpha fonts this will have format
                 * `CAIRO_FORMAT_A8` and for color fonts (i.e. emoji)
                 * it will have `CAIRO_FORMAT_ARGB32`.
                 */
                cairo_format_t cairo_format;
                cairo_font_face_t *cairo_face;
                cairo_t *cr;

                int width, height;
                struct ivec2 cursor;
                struct font *font;

                bool is_full;

                int next_line;
                unsigned num_glyph;

                int id;
        } **sprite_map;

        unsigned capacity_sprite_map;
        unsigned num_sprite_map;

        struct glyph *glyph;

        unsigned capacity_glyph;
        unsigned num_glyph;
};

struct glyph_manager *glyph_manager_create(void);
int glyph_manager_init(struct glyph_manager *m);
int glyph_manager_destroy(struct glyph_manager *m);
int glyph_manager_show(struct glyph_manager *m);

struct glyph_sheet glyph_manager_get_glyph_sheet(struct glyph_manager *m,
                                                 int glyph_sheet);
struct glyph glyph_manager_generate_glyph(struct glyph_manager *m,
                                          struct font *font,
                                          int glyph_id,
                                          int font_size);
