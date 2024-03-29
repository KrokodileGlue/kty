// SPDX-License-Identifier: GPL-2.0-only

/*
 * layout_engine.h
 * Copyright (C) 2023 Taylor West
 *
 * This file contains data structure and function declarations
 * exposing a public API for the generation of text layout
 * information.
 */

#pragma once

#include "cpu_cell.h"
#include "gpu_cell.h"
#include "glyph_manager.h"

struct basic_font_info {
        int cw;
        int ch;
        int ascender;
};

struct layout_engine;

struct layout_engine *layout_engine_create(void);
int layout_engine_show(struct layout_engine *e);
int layout_engine_destroy(struct layout_engine *e);
int layout_engine_add_font_from_name(struct layout_engine *e,
                                     const char *name,
                                     int font_size);
int layout(struct layout_engine *e,
           struct cpu_cell *cells, unsigned num_cells,
           struct glyph *glyphs, unsigned *num_glyphs,
           int pt_size);
int layout_engine_get_basic_font_info(struct layout_engine *e,
                                      struct basic_font_info *info,
                                      int pt_size);
struct glyph_manager *layout_engine_get_glyph_manager(struct layout_engine *e);
