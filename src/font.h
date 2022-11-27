#ifndef FONT_H
#define FONT_H

#include <stdint.h>

#include <GL/glew.h>
#include <freetype/freetype.h>

#include "util.h"
#include "sprite.h"

enum {
        GLYPH_WRAP      = 1 << 0,
        GLYPH_UNDERLINE = 1 << 1,
        GLYPH_BOLD      = 1 << 2,
        GLYPH_ITALIC    = 1 << 3,
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

struct font {
        const char *path;         /* The path that this font was loaded from. */
        int is_color_font;

        FT_Face face;
        FT_Render_Mode render_mode;

        char *sprite_buffer;
        GLuint sprite_texture;
        int spritemap_dirty;

        char *vertices;
        char *textures;
        char *colors;
        GLuint vbo_vertices;
        GLuint vbo_textures;
        GLuint vbo_colors;
        int num_glyphs_in_vbo;

        struct sprite glyph[NUM_GLYPH];
        int num_glyph;              /* The number of glyphs in the spritemap. */

        int pixel_size;
        int load_flags;
};

#endif
