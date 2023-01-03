#pragma once

#include <stdint.h>

#include <harfbuzz/hb.h>

struct font_manager;
struct font;

struct font_manager *font_manager_create(void);
int font_manager_init(struct font_manager *m);
int font_manager_destroy(struct font_manager *m);
int font_manager_add_font_from_name(struct font_manager *m, const char *name);
int font_manager_get_sizes(struct font_manager *m, int *cw, int *ch);
hb_font_t *font_manager_get_hb_font(struct font *font);
char *font_manager_get_font_name(struct font *font);
struct font *font_manager_get_font(struct font_manager *m,
                                   uint32_t c,
                                   int font_size);
