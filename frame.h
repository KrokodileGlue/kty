#ifndef FRAME_H
#define FRAME_H

#include <stdint.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <freetype/tttables.h>

enum {
        BEL = 0x07,
        BS = 0x08,
        HT = 0x09,
        LF = 0x0A,
        VT = 0x0B,
        FF = 0x0C,
        CR = 0x0D,
        ESC = 0x1B,
        DEL = 0x7F,
};

enum {
        GLYPH_WRAP = 1,
        GLYPH_UNDERLINE = 1 << 1,
        GLYPH_BOLD = 1 << 2,
        GLYPH_ITALIC = 1 << 3,
        GLYPH_DIM = 1 << 4,
        GLYPH_BLINKING = 1 << 5,
        GLYPH_INVERSE = 1 << 5,
        GLYPH_WIDE = 1 << 6,
        GLYPH_MAX = 1 << 7,
};

struct glyph {
        uint32_t c;
        int mode;
        int fg, bg;
};

struct sprite {
        uint32_t c;
        struct font *font;
        FT_Glyph_Metrics metrics;
        int bitmap_top;
        float tex_coords[4];
        int height;
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

enum {
        CURSOR_DEFAULT = 0,
        CURSOR_ORIGIN = 1,
        CURSOR_WRAPNEXT = 1 << 1,
};

struct cursor {
        int x, y, mode, state;
        int fg, bg;
};

struct window {
        int width, height;
        int cw, ch;
};

struct font_renderer {
        /* FreeType */
        FT_Library ft;

        /* TODO: Use a hashtable for this. */
        struct sprite glyph[NUM_GLYPH];
        int num_glyph;

        /* Fonts */
        struct font fonts[MAX_FONTS];
        int num_fonts;

        /* VBO for rendering decoration and background colors */
        GLuint vbo_decoration;
        GLuint vbo_decoration_color;
        char *decoration;
        char *decoration_color;
        unsigned num_decoration;
};

enum {
        ESC_START = 1,
        ESC_CSI = 1 << 1,
};

enum {
        MODE_CURSOR_VISIBLE = 1,
        MODE_CRLF = 1 << 1,
};

struct frame {
        /* This will be a pointer to a global font manager eventually. */
        struct font_renderer font;

        /* PTY */
        int master;

        /* OpenGL */
        GLuint program;

        GLint attribute_coord;
        GLint attribute_decoration_color;
        GLint attribute_color;
        GLint uniform_tex;
        GLint uniform_is_solid;
        GLint uniform_is_color;

        /* State */
        struct window w;
        int col, row;
        int top, bot; /* Required for tsetscroll */
        struct cursor c;
        struct glyph **line;

        int shell_done; /* bazinga */

        /* Escape sequence state machine */
        struct {
                char buf[1024];
                long arg[ESC_ARG_SIZE];
                unsigned len;
                int narg;
                int priv;
                int mode[2];
        } csi;

        int mode;
        int esc;

        /*
         * This could mean new characters to display or a change in
         * the position/display of the cursor.
         */
        int dirty_display;
};

struct color {
        GLfloat r, g, b;
};

#endif
