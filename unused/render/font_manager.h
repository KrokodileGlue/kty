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

#include <harfbuzz/hb.h>

struct font_manager;
struct font;

struct font_manager *font_manager_create(void);
int font_manager_init(struct font_manager *m);
int font_manager_destroy(struct font_manager *m);
int font_manager_show(struct font_manager *m);
int font_manager_add_font_from_name(struct font_manager *m, const char *name, int font_size);
int font_manager_get_sizes(struct font_manager *m, int *cw, int *ch);

/*
 * Functions for retrieving font properties.
 */
hb_font_t *font_manager_get_hb_font(struct font *font);
char *font_manager_get_font_name(struct font *font);
int font_manager_get_font_pt_size(struct font *font);
bool font_manager_is_font_color(struct font *font);
void font_manager_describe_font(struct font *font);

struct font *font_manager_get_font(struct font_manager *m,
                                   uint32_t c,
                                   int font_size);
