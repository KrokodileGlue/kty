// SPDX-License-Identifier: GPL-2.0-only

/*
 * font_manager.h
 * Copyright (C) 2022 Taylor West
 *
 * This file contains data structure and function declarations
 * exposing a public API for the management of font data.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <freetype/freetype.h>
#include <harfbuzz/hb.h>

struct font_manager;

struct font {
        char *path;
        char *name;

        FT_Face ft_face;
        hb_font_t *hb_font;

        bool has_color;
        bool is_fixed_width;
        bool bold;
        bool italic;

        int size;

        struct font *next;
};

struct font_manager *font_manager_create(void);
int font_manager_init(struct font_manager *m);
int font_manager_destroy(struct font_manager *m);
int font_manager_show(struct font_manager *m);
int font_manager_add_font_from_name(struct font_manager *m, const char *name, int font_size);
int font_manager_get_sizes(struct font_manager *m, int *cw, int *ch);
void font_manager_describe_font(struct font *font);
struct font *font_manager_get_font(struct font_manager *m,
                                   uint32_t *seq,
                                   unsigned len,
                                   bool bold,
                                   bool italic,
                                   int font_size);
