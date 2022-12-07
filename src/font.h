#pragma once

#include <stdint.h>

#include <GL/glew.h>
#include <freetype/freetype.h>

#include "util.h"
#include "sprite.h"

enum {
        FONT_REGULAR = 0,
        FONT_BOLD = 1 << 0,
        FONT_ITALIC = 1 << 1,
};

struct font {
        const char *path;         /* The path that this font was loaded from. */
        int is_color_font;

        FT_Face face;
        FT_Render_Mode render_mode;

        char *sprite_buffer;
        int sprite_buffer_len;
        int spritemap_dirty;

        struct sprite cell[MAX_SPRITES_IN_FONT];
        int num_cell;              /* The number of cells in the spritemap. */

        int pixel_size;
        int load_flags;

        int type;
};

struct font_manager {
        /* FreeType */
        FT_Library ft;

        /* TODO: Use a hashtable for this. */
        struct sprite cell[NUM_CELL];
        int num_cell;

        /* Fonts */
        struct font fonts[MAX_FONTS];
        int num_fonts;
};

int font_manager_init(struct font_manager *m);
struct sprite *get_sprite(struct font_manager *r, uint32_t c, int mode, int font_size);
void font_get_dimensions(struct font_manager *m, int *cw, int *ch, int font_size);
