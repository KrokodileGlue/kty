// SPDX-License-Identifier: GPL-2.0-only

/*
 * glyph_manager.h
 * Copyright (C) 2022 Taylor West
 *
 * This file contains data structure and function declarations
 * exposing a public API for the management of glyph data.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "config.h"
#include "cpu_cell.h"
#include "vec.h"

/*
 * Some notes on vocabulary:
 *
 * A "glyph" is a collection of information related to the display of
 * a single character from a font at a specific size. A glyph contains:
 *
 * + The position and dimensions of the bounding box
 * + The id of the glyph sheet where the glyph's sprite may be found
 * + The coordinates of the sprite in the sheet
 *
 * Whereas a "sprite" is really just the binary alpha/RGBA data which
 * you can actually use to display the glyph, as its stored in a
 * specific sprite sheet.
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

        bool bold;
        bool italic;

        struct font *font;

        struct ivec2 size;
        struct ivec2 vertices[6];
        struct ivec2 sprite_coordinates[6];
};

struct glyph_manager;

struct glyph_manager *glyph_manager_create(void);
int glyph_manager_show(struct glyph_manager *m);
int glyph_manager_init(struct glyph_manager *m);
int glyph_manager_destroy(struct glyph_manager *m);
int glyph_manager_add_font_from_name(struct glyph_manager *m,
                                     const char *name,
                                     int font_size);
struct glyph_sheet glyph_manager_get_glyph_sheet(struct glyph_manager *m,
                                                 int glyph_sheet);
struct glyph *glyph_manager_generate_glyph(struct glyph_manager *m,
                                           uint32_t *text,
                                           unsigned len,
                                           bool bold,
                                           bool italic,
                                           int font_size);
