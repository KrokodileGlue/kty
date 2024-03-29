#define _XOPEN_SOURCE 600
#include <wchar.h>

#include "render.h"

#include <stdbool.h>
#include <stdio.h>

#include "gl.h"                 /* bind_attribute_to_program, bind_unifo... */
#include "term.h"
#include "window.h"

int render_init(struct font_renderer *r, struct font_manager *m, struct color *color256)
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

        const char vs2[] = "#version 120\n\
attribute vec4 coord;\n\
varying vec2 tcoord;\n\
uniform vec2 scale;\n\
uniform vec2 offset;\n\
void main(void) {\n\
        vec2 shift = vec2(scale.x, scale.y);\n\
        gl_Position = vec4(coord.xy * scale + offset + shift, 0, 1);\n\
        tcoord = coord.zw;\n\
}";

        const char fs2[] = "#version 120\n\
varying vec2 tcoord;\n\
uniform sampler2D tex;\n\
void main(void) {\n\
    gl_FragColor = texture2D(tex, tcoord.xy);\n\
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

        /* Now check that everything compiled and linked okay. */
        GLint link_ok = GL_FALSE;
        glGetProgramiv(r->program, GL_LINK_STATUS, &link_ok);

        if (!link_ok) {
                fprintf(stderr, "glLinkProgram:");
                print_gl_error_log(r->program);
                return 1;
        }

        GLuint gvs2 = create_shader(vs2, GL_VERTEX_SHADER);
        GLuint gfs2 = create_shader(fs2, GL_FRAGMENT_SHADER);

        if (!gvs2 || !gfs2) return 1;

        r->ui_program = glCreateProgram();
        glAttachShader(r->ui_program, gvs2);
        glAttachShader(r->ui_program, gfs2);
        glLinkProgram(r->ui_program);

        glGetProgramiv(r->ui_program, GL_LINK_STATUS, &link_ok);

        if (!link_ok) {
                fprintf(stderr, "glLinkProgram:");
                print_gl_error_log(r->ui_program);
                return 1;
        }

        r->ui_attribute_coord = bind_attribute_to_program(r->ui_program, "coord");

        r->attribute_coord = bind_attribute_to_program(r->program, "coord");
        r->attribute_decoration_color = bind_attribute_to_program(r->program, "decoration_color");
        r->attribute_color = bind_attribute_to_program(r->program, "tex_color");

        r->uniform_ui_tex = bind_uniform_to_program(r->ui_program, "tex");
        r->uniform_ui_scale = bind_uniform_to_program(r->ui_program, "scale");
        r->uniform_ui_offset = bind_uniform_to_program(r->ui_program, "offset");

        r->uniform_tex = bind_uniform_to_program(r->program, "tex");
        r->uniform_is_solid = bind_uniform_to_program(r->program, "is_solid");
        r->uniform_is_color = bind_uniform_to_program(r->program, "is_color");

        /* Enabling blending allows us to use alpha textures. */
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        /* Generate the underline VBO. */
        glGenBuffers(1, &r->vbo_decoration);
        r->decoration = calloc(NUM_CELL * 2, 6 * 2 * sizeof(GLfloat));
        glGenBuffers(1, &r->vbo_decoration_color);
        r->decoration_color = calloc(NUM_CELL * 2, 6 * 3 * sizeof(GLfloat));
        r->num_fonts = m->num_fonts;

        struct {
                GLfloat x, y, s, t;
        } coord[6] = {
                { -1, -1, 0, 0 },
                {  1, -1, 1, 0 },
                { -1,  1, 0, 1 },
                { -1,  1, 0, 1 },
                {  1,  1, 1, 1 },
                {  1, -1, 1, 0 },
        };

        glGenBuffers(1, &r->vbo_quad);
        glBindBuffer(GL_ARRAY_BUFFER, r->vbo_quad);
        glEnableVertexAttribArray(r->ui_attribute_coord);

        glBufferData(GL_ARRAY_BUFFER,
                6 * 4 * sizeof(GLfloat),
                coord,
                GL_DYNAMIC_DRAW);

        glVertexAttribPointer(r->ui_attribute_coord,
                              4,
                              GL_FLOAT,
                              GL_FALSE,
                              4 * sizeof(GLfloat),
                              0);

        /*
         * So the renderer needs to keep track of some per-font data, including
         * each of these VBOs and a spritemap texture.
         */
        for (int i = 0; i < r->num_fonts; i++) {
                struct font_data *f = r->fonts + i;

                *f = (struct font_data){
                        .vertices = calloc(NUM_CELL * 2, 6 * 2 * sizeof(GLfloat)),
                        .textures = calloc(NUM_CELL * 2, 6 * 3 * sizeof(GLfloat)),
                        .colors = calloc(NUM_CELL * 2, 6 * 3 * sizeof(GLfloat)),
                        .is_color_font = m->fonts[i].is_color_font,
                        .font = m->fonts + i,
                };

                glGenBuffers(1, &f->vbo_vertices);
                glGenBuffers(1, &f->vbo_textures);
                glGenBuffers(1, &f->vbo_colors);

                glGenTextures(1, &f->sprite_texture);
        }

        r->m = m;
        r->color256 = color256;
        r->width = 800;
        r->height = 800;

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

struct color get_color_from_index(struct font_renderer *r, int i)
{
        if (i >= 0 && i < 256) return r->color256[i];
        i -= 256;
        int R = (i >> 16) & 0xFF;
        int G = (i >> 8) & 0xFF;
        int B = (i >> 0) & 0xFF;
        return (struct color){ R / 255.0, G / 255.0, B / 255.0 };
}

/*
 * Renders a `struct cell` into the current termbuffer.
 */
int render_cell(struct font_renderer *r, uint32_t c, struct cell_attr attr,
                int x0, int y0, int cw, int ch, int width, int height,
                int font_size)
{
        if (attr.mode & CELL_DUMMY) return 0;

        struct sprite *sprite = get_sprite(r->m, c, attr.mode, font_size);

        if (!sprite) {
                fprintf(stderr, "No cell found for U+%x\n", c);
                return 1;
        }

        FT_Glyph_Metrics metrics = sprite->metrics;

        float sx = 2.0 / width;
        float sy = 2.0 / height;

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

        int bfg = attr.fg;
        int bbg = attr.bg;

        if (attr.mode & CELL_INVERSE) {
                if (bbg < 0) bbg = 16;
                if (bfg < 0) bfg = 255;

                int tmp = bfg;
                bfg = bbg;
                bbg = tmp;
        }

        struct color fg = get_color_from_index(r, bfg);
        struct color bg = get_color_from_index(r, bbg);

        struct color col[] = { fg, fg, fg, fg, fg, fg };

        struct font_data *font = r->fonts + sprite->font;

        memcpy(font->vertices + font->num_cells_in_vbo * sizeof box, box, sizeof box);
        memcpy(font->textures + font->num_cells_in_vbo * sizeof tex, tex, sizeof tex);
        memcpy(font->colors + font->num_cells_in_vbo * sizeof col, col, sizeof col);
        font->num_cells_in_vbo++;

        if (bbg != -1)
                render_rectangle(r,
                                 y + ch * sy,
                                 y - LINE_SPACING * sy,
                                 x + cw * sx,
                                 x,
                                 bg);

        if (attr.mode & CELL_UNDERLINE)
                render_rectangle(r,
                                 y - 3 * sy + 1 * sy,
                                 y - 3 * sy,
                                 x,
                                 x + cw * sx,
                                 (struct color){1, 1, 1});

        return 0;
}

void render_cursor(struct font_renderer *r, struct cursor *cursor, int cw, int ch, int width, int height, bool wide)
{
        int x = cursor->x, y = cursor->y;

        float sx = 2.0 / (float)width;
        float sy = 2.0 / (float)height;

        float w = -1 + (cw * x) * sx;
        float n = 1 - (ch * y) * sy - LINE_SPACING * y * sy;
        float s = n - ch * sy - LINE_SPACING * sy;
        float e = w + cw * sx;

        /* TODO: Default cursor color customization. */
        struct color c = (struct color){ 0, 0.5, 0.5 };

        switch (cursor->style) {
                case CURSOR_STYLE_BLINKING_BLOCK:
                        /* TODO: Implement blinking block cursor. */
                case CURSOR_STYLE_DEFAULT:
                case CURSOR_STYLE_STEADY_BLOCK:
                        /* Thicken the cursor. */
                        if (wide) e += cw * sx;
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

void render_quad(struct font_renderer *r, int x0, int y0, int x1, int y1, GLuint tex)
{
        glUseProgram(r->ui_program);

        glUniform2f(r->uniform_ui_scale,
                    (float)(x1 - x0) / (float)r->width,
                    (float)(y1 - y0) / (float)r->height);

        glUniform2f(r->uniform_ui_offset,
                    -1.0 + 2.0 * (float)x0 / (float)r->width,
                    -1.0 + 2.0 * (float)y0 / (float)r->height);

        /* _printf("%d,%d,%d,%d\n", x0, y0, x1, y1); */
        /* _printf("scale=%f,%f offset=%f,%f\n", (float)(x1 - x0) / (float)r->width, */
        /*         (float)(y1 - y0) / (float)r->height, */
        /*         -1.0 + 2.0 * (float)x0 / (float)r->width, */
        /*         -1.0 + 2.0 * (float)y0 / (float)r->height); */

        glBindBuffer(GL_ARRAY_BUFFER, r->vbo_quad);
        glEnableVertexAttribArray(r->ui_attribute_coord);
        glVertexAttribPointer(r->ui_attribute_coord,
                              4,
                              GL_FLOAT,
                              GL_FALSE,
                              4 * sizeof(GLfloat),
                              0);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex);
        glGenerateMipmap(GL_TEXTURE_2D);

        glDrawArrays(GL_TRIANGLES, 0, 6);
}

void render_wterm(struct font_renderer *r, struct wterm *wt)
{
        struct term *t = wt->term;
        struct grid *g = t->g;

        /* TODO: Move this somewhere sensible. */
        glBindTexture(GL_TEXTURE_2D, wt->tex_color_buffer);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, wt->width,
                     wt->height, 0, GL_RGB, GL_UNSIGNED_BYTE,
                     NULL);

        /* TODO: Clean up the framebuffer. */
        glBindFramebuffer(GL_FRAMEBUFFER, wt->framebuffer);
        glViewport(0, 0, wt->width, wt->height);
        glUseProgram(r->program);

        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        /* Reset the VBO render state. */
        for (int i = 0; i < r->num_fonts; i++)
                r->fonts[i].num_cells_in_vbo = 0;

        r->num_decoration = 0;

        for (int i = 0; i < g->row; i++)
                for (int j = 0; j < g->col; j++)
                        if (g->line[i][j])
                                render_cell(r, g->line[i][j], g->attr[i][j], j, i, wt->cw, wt->ch, wt->width, wt->height, wt->font_size);

        /* Add the cursor to the decoration VBO. */
        if (t->mode & MODE_CURSOR_VISIBLE)
                render_cursor(r, t->c, wt->cw, wt->ch, wt->width, wt->height, g->attr[t->c->y][t->c->x].mode & CELL_WIDE);

        /* Render the quads. */
        glUniform1i(r->uniform_is_solid, 1);

        glBindBuffer(GL_ARRAY_BUFFER, r->vbo_decoration);
        glEnableVertexAttribArray(r->attribute_coord);

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
         * So each cell has been rendered into its font's spritesheet at this
         * point.
         */

        for (int i = 0; i < r->num_fonts; i++) {
                struct font_data *font = r->fonts + i;

                glBindBuffer(GL_ARRAY_BUFFER, font->vbo_vertices);
                glEnableVertexAttribArray(r->attribute_coord);

                glBufferData(GL_ARRAY_BUFFER,
                             font->num_cells_in_vbo * 6 * 2 * sizeof(GLfloat),
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

                glBufferData(GL_ARRAY_BUFFER,
                             font->num_cells_in_vbo * 6 * 3 * sizeof(GLfloat),
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

                glBufferData(GL_ARRAY_BUFFER,
                             font->num_cells_in_vbo * 6 * 3 * sizeof(GLfloat),
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

                /* TODO: Don't use a flag, use an update handler */
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

                glDrawArrays(GL_TRIANGLES, 0, font->num_cells_in_vbo * 6);
        }
}
