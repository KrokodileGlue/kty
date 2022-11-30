#ifndef FONT_H
#define FONT_H

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

        struct sprite glyph[NUM_GLYPH];
        int num_glyph;              /* The number of glyphs in the spritemap. */

        int pixel_size;
        int load_flags;

        int type;
};

struct font_manager {
        /* FreeType */
        FT_Library ft;

        /* TODO: Use a hashtable for this. */
        struct sprite glyph[NUM_GLYPH];
        int num_glyph;

        /* Fonts */
        struct font fonts[MAX_FONTS];
        int num_fonts;
};

int font_manager_init(struct font_manager *m, int *cw, int *ch);
struct sprite *get_sprite(struct font_manager *r, struct glyph c);

#endif
