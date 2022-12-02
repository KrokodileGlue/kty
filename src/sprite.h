#ifndef SPRITE_H
#define SPRITE_H

#include <stdint.h>
#include <freetype/freetype.h>

enum {
        /*
         * VOLATILE: CELL_BOLD and CELL_ITALIC need to be the same as
         * FONT_BOLD and FONT_ITALIC.
         */
        CELL_BOLD      = 1 << 0,
        CELL_ITALIC    = 1 << 1,
        CELL_WRAP      = 1 << 2,
        CELL_UNDERLINE = 1 << 3,
        CELL_DIM       = 1 << 4,
        CELL_BLINKING  = 1 << 5,
        CELL_INVERSE   = 1 << 6,
        CELL_WIDE      = 1 << 7,
        CELL_DUMMY     = 1 << 8,
        CELL_MAX       = CELL_DUMMY,
};

struct cell {
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
