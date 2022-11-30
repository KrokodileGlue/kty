#define _XOPEN_SOURCE 600
#include <wchar.h>

#include "render.h"

#include <stdio.h>

#include "gl.h"                 /* bind_attribute_to_program, bind_unifo... */
#include "frame.h"

int render_init(struct font_renderer *r, struct font_manager *m)
{
        const char vs[] = "#version 120\n\
attribute vec2 coord;\n\
attribute vec3 decoration_color;\n\
attribute vec3 tex_color;\n\
varying vec3 dec_color;\n\
varying vec3 tcolor;\n\
void main(void) {\n\
        gl_Position = vec4(coord.xy, 0, 1);\n\
        dec_color = decoration_color;\n\
        tcolor = tex_color;\n\
}";

        const char fs[] = "#version 120\n\
varying vec3 dec_color;\n\
varying vec3 tcolor;\n\
uniform sampler2D tex;\n\
uniform int is_solid;\n\
uniform int is_color;\n\
void main(void) {\n\
        if (is_solid == 1) {\n\
                gl_FragColor = vec4(dec_color, 1);\n\
        } else if (is_color == 1) {\n\
                //gl_FragColor = vec4(1, 1, 0.5, 1);\n\
                gl_FragColor = texture2D(tex, dec_color.xy);\n\
                //gl_FragColor = vec4(dec_color.xy / 0.0625, 1, 1);\n\
        } else {\n\
                gl_FragColor = vec4(tcolor, texture2D(tex, dec_color.xy).a);\n\
        }\n\
}";

        /* Compile each shader. */
        GLuint gvs = create_shader(vs, GL_VERTEX_SHADER);
        GLuint gfs = create_shader(fs, GL_FRAGMENT_SHADER);

        if (!gvs || !gfs) return 1;

        /* Create the program and link the shaders. */
        r->program = glCreateProgram();
        glAttachShader(r->program, gvs);
        glAttachShader(r->program, gfs);
        glLinkProgram(r->program);
        glUseProgram(r->program);

        /* Now check that everything compiled and linked okay. */
        GLint link_ok = GL_FALSE;
        glGetProgramiv(r->program, GL_LINK_STATUS, &link_ok);

        if (!link_ok) {
                fprintf(stderr, "glLinkProgram:");
                print_gl_error_log(r->program);
                return 1;
        }

        r->attribute_coord = bind_attribute_to_program(r->program, "coord");
        r->attribute_decoration_color = bind_attribute_to_program(r->program, "decoration_color");
        r->attribute_color = bind_attribute_to_program(r->program, "tex_color");

        r->uniform_tex = bind_uniform_to_program(r->program, "tex");
        r->uniform_is_solid = bind_uniform_to_program(r->program, "is_solid");
        r->uniform_is_color = bind_uniform_to_program(r->program, "is_color");

        /* Enabling blending allows us to use alpha textures. */
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        /* Generate the underline VBO. */
        glGenBuffers(1, &r->vbo_decoration);
        r->decoration = calloc(NUM_GLYPH * 2, 6 * 2 * sizeof(GLfloat));
        glGenBuffers(1, &r->vbo_decoration_color);
        r->decoration_color = calloc(NUM_GLYPH * 2, 6 * 3 * sizeof(GLfloat));
        r->num_fonts = m->num_fonts;

        /*
         * So the renderer needs to keep track of some per-font data, including
         * each of these VBOs and a spritemap texture.
         */
        for (int i = 0; i < r->num_fonts; i++) {
                struct font_data *f = r->fonts + i;

                *f = (struct font_data){
                        .vertices = calloc(NUM_GLYPH * 2, 6 * 2 * sizeof(GLfloat)),
                        .textures = calloc(NUM_GLYPH * 2, 6 * 3 * sizeof(GLfloat)),
                        .colors = calloc(NUM_GLYPH * 2, 6 * 3 * sizeof(GLfloat)),
                        .is_color_font = m->fonts[i].is_color_font,
                        .font = m->fonts + i,
                };

                glGenBuffers(1, &f->vbo_vertices);
                glGenBuffers(1, &f->vbo_textures);
                glGenBuffers(1, &f->vbo_colors);

                glGenTextures(1, &f->sprite_texture);
        }

        r->m = m;

        return 0;
}

void render_rectangle(struct font_renderer *r, float n, float s, float w,
        float e, struct color color)
{
        struct {
                GLfloat x, y;
        } box[6] = {
                { w, n },
                { e, n },
                { w, s },
                { e, s },
                { e, n },
                { w, s },
        };

        struct color col[] = { color, color, color, color, color, color };

        memcpy(r->decoration + r->num_decoration * sizeof box,
               box, sizeof box);
        memcpy(r->decoration_color + r->num_decoration * sizeof col,
               col, sizeof col);
        r->num_decoration++;
}

struct color get_color_from_index(int i, struct color regular)
{
        struct color c = regular;

        if (i >= 0 && i < 8) {
                c = (struct color){ i % 2, (i / 2) % 2, i / 4 };
        } else if (i >= 8 && i < 16) {
                i -= 8;
                c = (struct color){ i % 2, (i / 2) % 2, i / 4 };
        } else if (i >= 16 && i < 256 - 24) {
                i -= 16;
                int B = i % 6;
                int G = (i / 6) % 6;
                int R = (i / 36) % 6;
                c = (struct color){ R / 6.0, G / 6.0, B / 6.0 };
        } else if (i >= 256 - 24 && i < 256) {
                i -= 256 - 24;
                float res = i / 24.0;
                c = (struct color){ res, res, res };
        } else if (i >= 256) {
                i -= 256;
                int R = (i >> 16) & 0xFF;
                int G = (i >> 8) & 0xFF;
                int B = (i >> 0) & 0xFF;
                c = (struct color){ R / 255.0, G / 255.0, B / 255.0 };
        }

        return c;
}

int render_glyph(struct font_renderer *r, struct glyph g, int x0, int y0, int cw, int ch)
{
        if (g.mode & GLYPH_DUMMY) return 0;

        uint32_t c = g.c;
        struct sprite *sprite = get_sprite(r->m, c);

        if (!sprite) {
                fprintf(stderr, "No glyph found for U+%x\n", c);
                return 1;
        }

        FT_Glyph_Metrics metrics = sprite->metrics;

        float sx = 2.0 / r->width;
        float sy = 2.0 / r->height;

        /* Calculate the vertex and texture coordinates. */
        float x = -1 + (cw * x0) * sx;
        float y = 1 - (ch * (1 + y0)) * sy - LINE_SPACING * y0 * sy;
        float x2 = x + metrics.horiBearingX * 1.0/64.0 * sx;
        float y2 = -y - sprite->bitmap_top * sy;
        float w = metrics.width * 1.0/64.0 * sx;
        float h = metrics.height * 1.0/64.0 * sy;

        /*
         * HACK: The `+ 5 * sy` term allows five pixels of leeway when we're
         * resizing big characters. Theoretically with a proper monospace font
         * there shouldn't be any characters taller than the advance width, but
         * that doesn't seem to be the case. Deja Vu Sans Mono's j character is
         * taller than the vertical advance width, for example, and the line
         * drawing characters also tend to exceed the vertical advance width in
         * many fonts. This is mainly to deal with fonts like Noto Color Emoji
         * which have really giant characters which refuse to render at other
         * sizes and have to be forcibly scaled down.
         */
        if (h > ch * sy + 5 * sy) {
                float tmp = wcwidth(c) * (float)cw * sx + 0.1 * LINE_SPACING * sx;
                float ratio = tmp / w;
                w = tmp;
                h *= ratio;
                y2 = -y - ch * sy;
                if (ch * sy - h > 0)
                        y2 += ch * sy - h;
        }

        struct {
                GLfloat x, y;
        } box[6] = {
                { x2    , -y2     },
                { x2 + w, -y2     },
                { x2    , -y2 - h },

                { x2 + w, -y2 - h },
                { x2 + w, -y2     },
                { x2    , -y2 - h },
        };

        struct {
                GLfloat s, t, v;
        } tex[6] = {
                { sprite->tex_coords[0], sprite->tex_coords[1], 0 },
                { sprite->tex_coords[2], sprite->tex_coords[1], 0 },
                { sprite->tex_coords[0], sprite->tex_coords[3], 0 },

                { sprite->tex_coords[2], sprite->tex_coords[3], 0 },
                { sprite->tex_coords[2], sprite->tex_coords[1], 0 },
                { sprite->tex_coords[0], sprite->tex_coords[3], 0 },
        };

        /* TODO: Make default fg and other colors configurable. */

        struct color fg = get_color_from_index(g.fg, (struct color){ 1, 1, 1 });
        struct color bg = get_color_from_index(g.bg, (struct color){ 0, 0, 0 });

        if (g.mode & GLYPH_INVERSE) {
                struct color tmp = fg;
                fg = bg;
                bg = tmp;
        }

        struct color col[] = { fg, fg, fg, fg, fg, fg };

        struct font_data *font = r->fonts + sprite->font;

        memcpy(font->vertices + font->num_glyphs_in_vbo * sizeof box, box, sizeof box);
        memcpy(font->textures + font->num_glyphs_in_vbo * sizeof tex, tex, sizeof tex);
        memcpy(font->colors + font->num_glyphs_in_vbo * sizeof col, col, sizeof col);
        font->num_glyphs_in_vbo++;

        render_rectangle(r,
                y + ch * sy,
                y - LINE_SPACING * sy,
                x + cw * sx,
                x,
                bg);

        if (g.mode & GLYPH_UNDERLINE)
                render_rectangle(r,
                                 y - 3 * sy + 1 * sy,
                                 y - 3 * sy,
                                 x,
                                 x + cw * sx,
                                 (struct color){1, 1, 1});

        return 0;
}

void render_cursor(struct font_renderer *r, struct frame *f)
{
        int x = f->c.x, y = f->c.y;

        float sx = 2.0 / (float)r->width;
        float sy = 2.0 / (float)r->height;

        float w = -1 + (f->cw * x) * sx;
        float n = 1 - (f->ch * y) * sy - LINE_SPACING * y * sy;
        float s = n - f->ch * sy - LINE_SPACING * sy;
        float e = w + f->cw * sx;
        /* TODO: Default cursor color customization. */
        struct color c = (struct color){ 0, 0.5, 0.5 };

        switch (f->cursor_style) {
                case CURSOR_STYLE_BLINKING_BLOCK:
                        /* TODO: Implement blinking block cursor. */
                case CURSOR_STYLE_DEFAULT:
                case CURSOR_STYLE_STEADY_BLOCK:
                        /* Thicken the cursor. */
                        if (f->line[y][x].mode & GLYPH_WIDE)
                                e += f->cw * sx;
                        break;
                case CURSOR_STYLE_BLINKING_UNDERLINE:
                case CURSOR_STYLE_STEADY_UNDERLINE:
                        n = s + 3 * sy;
                        c = (struct color){ 1, 1, 1 };
                        break;
                case CURSOR_STYLE_BLINKING_BAR:
                case CURSOR_STYLE_STEADY_BAR:
                        e = w + 2 * sx;
                        c = (struct color){ 1, 1, 1 };
                        break;
        }

        render_rectangle(r, n, s, w, e, c);
}

void render_frame(struct font_renderer *r, struct frame *f)
{
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        if (r->dirty_display) {
                /* Reset the VBO render state. */
                for (int i = 0; i < r->num_fonts; i++)
                        r->fonts[i].num_glyphs_in_vbo = 0;

                r->num_decoration = 0;

                for (int i = 0; i < f->row; i++)
                        for (int j = 0; j < f->col; j++)
                                if (f->line[i][j].c)
                                        render_glyph(r, f->line[i][j], j, i, f->cw, f->ch);

                /* Add the cursor to the decoration VBO. */
                if (f->mode & MODE_CURSOR_VISIBLE)
                        render_cursor(r, f);

        }

        /* Render the quads. */
        glUniform1i(r->uniform_is_solid, 1);

        glBindBuffer(GL_ARRAY_BUFFER, r->vbo_decoration);
        glEnableVertexAttribArray(r->attribute_coord);

        if (r->dirty_display)
                glBufferData(GL_ARRAY_BUFFER,
                             r->num_decoration * 6 * 2 * sizeof(GLfloat),
                             r->decoration,
                             GL_DYNAMIC_DRAW);

        glVertexAttribPointer(r->attribute_coord,
                              2,
                              GL_FLOAT,
                              GL_FALSE,
                              2 * sizeof(GLfloat),
                              0);

        glBindBuffer(GL_ARRAY_BUFFER, r->vbo_decoration_color);
        glEnableVertexAttribArray(r->attribute_decoration_color);

        if (r->dirty_display)
                glBufferData(GL_ARRAY_BUFFER,
                             r->num_decoration * 6 * 3 * sizeof(GLfloat),
                             r->decoration_color,
                             GL_DYNAMIC_DRAW);

        glVertexAttribPointer(r->attribute_decoration_color,
                              3,
                              GL_FLOAT,
                              GL_FALSE,
                              3 * sizeof(GLfloat),
                              0);

        glDrawArrays(GL_TRIANGLES, 0, r->num_decoration * 6);

        /*
         * So each glyph has been rendered into its font's spritesheet at this
         * point.
         */

        for (int i = 0; i < r->num_fonts; i++) {
                struct font_data *font = r->fonts + i;

                glBindBuffer(GL_ARRAY_BUFFER, font->vbo_vertices);
                glEnableVertexAttribArray(r->attribute_coord);

                if (r->dirty_display)
                        glBufferData(GL_ARRAY_BUFFER,
                                     font->num_glyphs_in_vbo * 6 * 2 * sizeof(GLfloat),
                                     font->vertices,
                                     GL_DYNAMIC_DRAW);

                glVertexAttribPointer(r->attribute_coord,
                                      2,
                                      GL_FLOAT,
                                      GL_FALSE,
                                      2 * sizeof(GLfloat),
                                      0);

                glBindBuffer(GL_ARRAY_BUFFER, font->vbo_textures);
                glEnableVertexAttribArray(r->attribute_decoration_color);

                if (r->dirty_display)
                        glBufferData(GL_ARRAY_BUFFER,
                                     font->num_glyphs_in_vbo * 6 * 3 * sizeof(GLfloat),
                                     font->textures,
                                     GL_DYNAMIC_DRAW);

                glVertexAttribPointer(r->attribute_decoration_color,
                                      3,
                                      GL_FLOAT,
                                      GL_FALSE,
                                      3 * sizeof(GLfloat),
                                      0);

                glBindBuffer(GL_ARRAY_BUFFER, font->vbo_colors);
                glEnableVertexAttribArray(r->attribute_color);

                if (r->dirty_display)
                        glBufferData(GL_ARRAY_BUFFER,
                                     font->num_glyphs_in_vbo * 6 * 3 * sizeof(GLfloat),
                                     font->colors,
                                     GL_DYNAMIC_DRAW);

                glVertexAttribPointer(r->attribute_color,
                                      3,
                                      GL_FLOAT,
                                      GL_FALSE,
                                      3 * sizeof(GLfloat),
                                      0);

                glActiveTexture(GL_TEXTURE0 + i);
                glBindTexture(GL_TEXTURE_2D, font->sprite_texture);
                glUniform1i(r->uniform_tex, i);
                glUniform1i(r->uniform_is_solid, 0);

                struct font *f = font->font;

                if (f->spritemap_dirty) {
                        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                        f->is_color_font ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

                        /* Upload the sprite map. */

                        glTexImage2D(GL_TEXTURE_2D,
                                     0,
                                     f->is_color_font ? GL_RGBA8 : GL_ALPHA,
                                     2048,
                                     2048,
                                     0,
                                     f->is_color_font ? GL_BGRA : GL_ALPHA,
                                     GL_UNSIGNED_BYTE,
                                     f->sprite_buffer);

                        if (f->is_color_font) glGenerateMipmap(GL_TEXTURE_2D);

                        f->spritemap_dirty = 0;
                }

                glUniform1i(r->uniform_is_color, !!font->is_color_font);

                glDrawArrays(GL_TRIANGLES, 0, font->num_glyphs_in_vbo * 6);
        }

        r->dirty_display = 0;
}
