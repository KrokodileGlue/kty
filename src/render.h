#ifndef RENDER_H
#define RENDER_H

struct font_renderer {
        /* FreeType */
        FT_Library ft;

        /* TODO: Use a hashtable for this. */
        struct sprite glyph[NUM_GLYPH];
        int num_glyph;

        /* Fonts */
        struct font fonts[MAX_FONTS];
        int num_fonts;

        /* OpenGL */
        GLuint program;

        GLint attribute_coord;
        GLint attribute_decoration_color;
        GLint attribute_color;
        GLint uniform_tex;
        GLint uniform_is_solid;
        GLint uniform_is_color;

        /* VBO for rendering decoration and background colors */
        GLuint vbo_decoration;
        GLuint vbo_decoration_color;
        char *decoration;
        char *decoration_color;
        unsigned num_decoration;

        /*
         * This could mean new characters to display or a change in
         * the position/display of the cursor.
         */
        int dirty_display;
};

#endif
