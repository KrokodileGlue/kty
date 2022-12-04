#ifndef RENDER_H
#define RENDER_H

#include <GL/glew.h>
#include <freetype/freetype.h>

#include "font.h"
#include "util.h"
#include "sprite.h"

struct term;

struct font_renderer {
        /* Pointer to the global font manager. */
        struct font_manager *m;

        /* OpenGL */
        GLuint program;
        GLuint ui_program;

        GLint ui_attribute_coord;

        GLint attribute_coord;
        GLint attribute_decoration_color;
        GLint attribute_color;
        GLint uniform_ui_tex;
        GLint uniform_tex;
        GLint uniform_is_solid;
        GLint uniform_is_color;

        GLuint vbo_quad;
        GLuint vbo_decoration;
        GLuint vbo_decoration_color;
        char *decoration;
        char *decoration_color;
        unsigned num_decoration;

        /* Width of height and window in pixels. */
        int width, height;

        /*
         * This could mean new characters to display or a change in
         * the position/display of the cursor.
         */
        int dirty_display;

        struct font_data {
                GLuint sprite_texture;
                GLuint vbo_vertices;
                GLuint vbo_textures;
                GLuint vbo_colors;
                char *vertices, *textures, *colors;
                int num_cells_in_vbo;
                int is_color_font;
                struct font *font;
        } fonts[MAX_FONTS];

        int num_fonts;

        struct color *color256;
};

int render_init(struct font_renderer *r, struct font_manager *m, struct color *color256);
void render_term(struct font_renderer *r, struct term *f);
void render_load_fonts(struct font_renderer *r);
void render_quad(struct font_renderer *r, GLuint tex);

#endif
