#ifndef SPRITE_H
#define SPRITE_H

#include <stdint.h>
#include <freetype/freetype.h>

struct sprite {
        uint32_t c;
        struct font *font;
        FT_Glyph_Metrics metrics;
        int bitmap_top;
        float tex_coords[4];
        int height;
};

#endif
