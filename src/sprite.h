#ifndef SPRITE_H
#define SPRITE_H

#include <stdint.h>
#include <freetype/freetype.h>

enum {
        /*
         * VOLATILE: GLYPH_BOLD and GLYPH_ITALIC need to be the same as
         * FONT_BOLD and FONT_ITALIC.
         */
        GLYPH_BOLD      = 1 << 0,
        GLYPH_ITALIC    = 1 << 1,
        GLYPH_WRAP      = 1 << 2,
        GLYPH_UNDERLINE = 1 << 3,
        GLYPH_DIM       = 1 << 4,
        GLYPH_BLINKING  = 1 << 5,
        GLYPH_INVERSE   = 1 << 6,
        GLYPH_WIDE      = 1 << 7,
        GLYPH_DUMMY     = 1 << 8,
        GLYPH_MAX       = GLYPH_DUMMY,
};

struct glyph {
        uint32_t c;
        int mode;
        int fg, bg;
};

struct sprite {
        uint32_t c;
        int mode;
        int font;
        struct texture *texture;
        FT_Glyph_Metrics metrics;
        int bitmap_top;
        float tex_coords[4];
        int height;
};

#endif
