#define _XOPEN_SOURCE 600

#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <inttypes.h>
#include <wchar.h>
#include <ctype.h>

#include <unistd.h>
#include <pthread.h> /* TODO: Windows support. */
#include <fcntl.h>
#include <sys/ioctl.h>

#include <GL/glew.h>
#include <GL/gl.h>

#include <GLFW/glfw3.h>

#include "util.h"
#include "frame.h"
#include "gl.h"
#include "t.h"
#include "utf8.h"
#include "window.h"

int init_gl_resources(struct frame *f)
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
        f->program = glCreateProgram();
        glAttachShader(f->program, gvs);
        glAttachShader(f->program, gfs);
        glLinkProgram(f->program);
        glUseProgram(f->program);

        /* Now check that everything compiled and linked okay. */
        GLint link_ok = GL_FALSE;
        glGetProgramiv(f->program, GL_LINK_STATUS, &link_ok);

        if (!link_ok) {
                fprintf(stderr, "glLinkProgram:");
                print_gl_error_log(f->program);
                return 1;
        }

        f->attribute_coord = bind_attribute_to_program(f->program, "coord");
        f->attribute_decoration_color = bind_attribute_to_program(f->program, "decoration_color");
        f->attribute_color = bind_attribute_to_program(f->program, "tex_color");

        f->uniform_tex = bind_uniform_to_program(f->program, "tex");
        f->uniform_is_solid = bind_uniform_to_program(f->program, "is_solid");
        f->uniform_is_color = bind_uniform_to_program(f->program, "is_color");

        /* Enabling blending allows us to use alpha textures. */
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        return 0;
}

struct sprite *get_sprite(struct frame *f, uint32_t c)
{
        for (int i = 0; i < f->font.num_glyph; i++)
                if (f->font.glyph[i].c == c)
                        return f->font.glyph + i;

        struct font *font = NULL;
        int load_flags = FT_LOAD_COLOR;

        for (int i = 0; i < f->font.num_fonts; i++) {
                struct font *fo = f->font.fonts + i;
                FT_Face face = fo->face;
                FT_Set_Pixel_Sizes(face, 0, FONT_SIZE);

                uint32_t glyph_index = FT_Get_Char_Index(face, c);
                if (!glyph_index) continue;

                if (fo->is_color_font) {
                        if (!face->num_fixed_sizes) return NULL;

                        int best_match = 0;
                        int diff = abs(FONT_SIZE - face->available_sizes[0].width);

                        for (int i = 1; i < face->num_fixed_sizes; i++) {
                                int ndiff = abs(FONT_SIZE - face->available_sizes[i].width);
                                if (ndiff < diff) {
                                        best_match = i;
                                        diff = ndiff;
                                }
                        }

                        FT_Select_Size(face, best_match);

                        if (FT_Load_Glyph(face, glyph_index, load_flags)) continue;
                        if (FT_Render_Glyph(face->glyph, fo->render_mode)) continue;
                } else {
                        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) continue;
                }

                font = fo;
                break;
        }

        if (!font && c != 0x25a1) return get_sprite(f, 0x25a1);

        FT_GlyphSlot slot = font->face->glyph;

#ifdef DEBUG
        unsigned char buf[5] = { 0 };
        utf8encode(c, buf, &(unsigned){0});
        _printf("Adding sprite U+%x (%s) to %s\n", c, buf, font->path);
#endif

        if (!slot->bitmap.buffer) {
                _printf("-> null buffer\n");

                /*
                 * TODO: This is a weird edge case that can probably be handled
                 * more cleanly. Basically the buffer will be NULL when a
                 * character like the common space (0x20) gets rendered by
                 * FreeType. The obvious solution is just oh let's return NULL
                 * because it doesn't need a sprite. That doesn't work because
                 * we might need the character advance or whatever, so we have
                 * to return SOMETHING. Also, new characters are allocated in
                 * the bitmap based on where the previous character was placed,
                 * so if we add a new sprite to this list with random texture
                 * coordinates then the spritemap allocation will go haywire.
                 * That's why we're returning a character with the same texture
                 * coordinates as the previous character; they're not actually
                 * used for rendering but they are used for spritemap
                 * allocation.
                 */
                struct sprite s = font->glyph[font->num_glyph - 1];
                font->glyph[font->num_glyph] = f->font.glyph[f->font.num_glyph] = (struct sprite){
                        .c = c,
                        .metrics = slot->metrics,
                        .bitmap_top = slot->bitmap_top,
                        .tex_coords = {s.tex_coords[0], s.tex_coords[1], s.tex_coords[2], s.tex_coords[3]},
                        .font = font,
                        .height = 0,
                };
                font->num_glyph++;

                return &f->font.glyph[f->font.num_glyph++];
        }

        /* TODO: Fix this haha. */
        int cw = slot->bitmap.width, ch = slot->bitmap.rows;
        int x = font->glyph[font->num_glyph - 1].tex_coords[2] * 2048 + 1;
        int y = font->glyph[font->num_glyph - 1].tex_coords[1] * 2048;

        int m = 0;
        for (int i = 0; i < font->num_glyph; i++)
                m = font->glyph[i].height > m ? font->glyph[i].height : m;

        _printf("\tStored at spritemap coordinates %d,%d\n", x, y);

        if (font->is_color_font && x + cw * 4 >= 2048 * 4) {
                x = 0;
                y += m;
        }
        if (!font->is_color_font && x >= 2048) {
                x = 0;
                y += m;
        }

        /* Write the sprite into the spritemap. */
        for (unsigned i = 0; i < slot->bitmap.rows; i++) {
                if (font->is_color_font) {
                        memcpy(font->sprite_buffer + (y + i) * 2048 * 4 + x * 4,
                               slot->bitmap.buffer + i * slot->bitmap.width  * 4,
                               slot->bitmap.width * 4);
                } else {
                        memcpy(font->sprite_buffer + (y + i) * 2048 + x,
                               slot->bitmap.buffer + i * slot->bitmap.width,
                               slot->bitmap.width);
                }
        }

        float fx0 = (float)x / 2048.0;
        float fx1 = (float)(x + slot->bitmap.width) / 2048.0;
        float fy0 = (float)y / 2048.0;
        float fy1 = (float)(y + slot->bitmap.rows) / 2048.0;

        font->glyph[font->num_glyph] = f->font.glyph[f->font.num_glyph] = (struct sprite){
                .c = c,
                .metrics = slot->metrics,
                .bitmap_top = slot->bitmap_top,
                .tex_coords = { fx0, fy0, fx1, fy1 },
                .font = font,
                .height = ch,
        };

        font->spritemap_dirty = 1;
        font->num_glyph++;

        return &f->font.glyph[f->font.num_glyph++];
}

void render_rectangle(struct frame *f, float n, float s, float w, float e, struct color color)
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

        memcpy(f->font.decoration + f->font.num_decoration * sizeof box,
               box, sizeof box);
        memcpy(f->font.decoration_color + f->font.num_decoration * sizeof col,
               col, sizeof col);
        f->font.num_decoration++;
}

int render_glyph(struct frame *f, struct glyph g, int x0, int y0)
{
        uint32_t c = g.c;
        struct sprite *sprite = get_sprite(f, c);

        if (!sprite) {
                fprintf(stderr, "No glyph found for U+%x\n", c);
                return 1;
        }

        FT_Glyph_Metrics metrics = sprite->metrics;

        float sx = 2.0 / f->w.width;
        float sy = 2.0 / f->w.height;

        /* Calculate the vertex and texture coordinates. */
        float x = -1 + (f->w.cw * x0) * sx;
        float y = 1 - (f->w.ch * (1 + y0)) * sy - LINE_SPACING * y0 * sy;
        float x2 = x + metrics.horiBearingX * 1.0/64.0 * sx;
        float y2 = -y - sprite->bitmap_top * sy;
        float w = metrics.width * 1.0/64.0 * sx;
        float h = metrics.height * 1.0/64.0 * sy;

        /*
         * The `+ 1 * sy` term allows one pixel of leeway when we're
         * resizing big characters. Theoretically with a proper
         * monospace font there shouldn't be any characters taller
         * than the advance width, but Deja Vu Sans Mono's j character
         * is taller than the vertical advance width.
         */
        if (h > f->w.ch * sy + 1 * sy) {
                float tmp = wcwidth(c) * (float)f->w.cw * sx + 0.1 * LINE_SPACING * sx;
                float ratio = tmp / w;
                w = tmp;
                h *= ratio;
                y2 = -y - f->w.ch * sy;
                if (f->w.ch * sy - h > 0)
                        y2 += f->w.ch * sy - h;
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
        struct color fg = (struct color){ 1, 1, 1 };

        if (g.fg >= 30) {
                fg = (struct color []){
                        { 0, 0, 0 },
                        { 1, 0, 0 },
                        { 0, 1, 0 },
                        { 1, 1, 0 },
                        { 0, 0, 1 },
                        { 1, 0, 1 },
                        { 0, 1, 1 },
                        { 1, 1, 1 },
                        { 1, 1, 1 },
                }[g.fg - 30];
        }

        struct color col[] = { fg, fg, fg, fg, fg, fg };

        struct font *font = sprite->font;

        memcpy(font->vertices + font->num_glyphs_in_vbo * sizeof box, box, sizeof box);
        memcpy(font->textures + font->num_glyphs_in_vbo * sizeof tex, tex, sizeof tex);
        memcpy(font->colors + font->num_glyphs_in_vbo * sizeof col, col, sizeof col);
        font->num_glyphs_in_vbo++;

        if (g.mode & GLYPH_UNDERLINE)
                render_rectangle(f,
                                 y - 3 * sy + 1 * sy,
                                 y - 3 * sy,
                                 x,
                                 x + f->w.cw * sx,
                                 (struct color){1, 1, 1});

        if (g.bg >= 40)
                render_rectangle(f,
                                 y + f->w.ch * sy,
                                 y - LINE_SPACING * sy,
                                 x + f->w.cw * sx,
                                 x,
                                 (struct color []){
                                         { 0, 0, 0 },
                                         { 1, 0, 0 },
                                         { 0, 1, 0 },
                                         { 1, 1, 0 },
                                         { 0, 0, 1 },
                                         { 1, 0, 1 },
                                         { 0, 1, 1 },
                                         { 0.5, 0.5, 0.5 },
                                         { 0.5, 0.5, 0.5 },
                                 }[g.bg - 40]);

        return 0;
}

void render_cursor(struct frame *f)
{
        int x = f->c.x, y = f->c.y;

        float sx = 2.0 / (float)f->w.width;
        float sy = 2.0 / (float)f->w.height;

        float w = -1 + (f->w.cw * x) * sx;
        float n = 1 - (f->w.ch * y) * sy - LINE_SPACING * y * sy;
        float s = n - f->w.ch * sy - LINE_SPACING * sy;
        float e = w + f->w.cw * sx;

        /* Thicken the cursor. */
        if (f->line[y][x].mode & GLYPH_WIDE)
                e += f->w.cw * sx;

        render_rectangle(f, n, s, w, e, (struct color){ 0, 0.5, 0.5});
}

void render(struct frame *f)
{
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        if (f->dirty_display) {
                /* Reset the VBO render state. */
                for (int i = 0; i < f->font.num_fonts; i++)
                        f->font.fonts[i].num_glyphs_in_vbo = 0;

                f->font.num_decoration = 0;

                /* Add the cursor to the decoration VBO. */
                if (f->mode & MODE_CURSOR_VISIBLE)
                        render_cursor(f);

                for (int i = 0; i < f->row; i++)
                        for (int j = 0; j < f->col; j++)
                                if (f->line[i][j].c)
                                        render_glyph(f, f->line[i][j], j, i);
        }

        /* Render the quads. */
        glUniform1i(f->uniform_is_solid, 1);

        glBindBuffer(GL_ARRAY_BUFFER, f->font.vbo_decoration);
        glEnableVertexAttribArray(f->attribute_coord);

        if (f->dirty_display)
                glBufferData(GL_ARRAY_BUFFER,
                             f->font.num_decoration * 6 * 2 * sizeof(GLfloat),
                             f->font.decoration,
                             GL_DYNAMIC_DRAW);

        glVertexAttribPointer(f->attribute_coord,
                              2,
                              GL_FLOAT,
                              GL_FALSE,
                              2 * sizeof(GLfloat),
                              0);

        glBindBuffer(GL_ARRAY_BUFFER, f->font.vbo_decoration_color);
        glEnableVertexAttribArray(f->attribute_decoration_color);

        if (f->dirty_display)
                glBufferData(GL_ARRAY_BUFFER,
                             f->font.num_decoration * 6 * 3 * sizeof(GLfloat),
                             f->font.decoration_color,
                             GL_DYNAMIC_DRAW);

        glVertexAttribPointer(f->attribute_decoration_color,
                              3,
                              GL_FLOAT,
                              GL_FALSE,
                              3 * sizeof(GLfloat),
                              0);

        glDrawArrays(GL_TRIANGLES, 0, f->font.num_decoration * 6);

        /*
         * So each glyph has been rendered into its font's spritesheet at this
         * point.
         */

        for (int i = 0; i < f->font.num_fonts; i++) {
                struct font *font = f->font.fonts + i;

                glBindBuffer(GL_ARRAY_BUFFER, font->vbo_vertices);
                glEnableVertexAttribArray(f->attribute_coord);

                if (f->dirty_display)
                        glBufferData(GL_ARRAY_BUFFER,
                                     font->num_glyphs_in_vbo * 6 * 2 * sizeof(GLfloat),
                                     font->vertices,
                                     GL_DYNAMIC_DRAW);

                glVertexAttribPointer(f->attribute_coord,
                                      2,
                                      GL_FLOAT,
                                      GL_FALSE,
                                      2 * sizeof(GLfloat),
                                      0);

                glBindBuffer(GL_ARRAY_BUFFER, font->vbo_textures);
                glEnableVertexAttribArray(f->attribute_decoration_color);

                if (f->dirty_display)
                        glBufferData(GL_ARRAY_BUFFER,
                                     font->num_glyphs_in_vbo * 6 * 3 * sizeof(GLfloat),
                                     font->textures,
                                     GL_DYNAMIC_DRAW);

                glVertexAttribPointer(f->attribute_decoration_color,
                                      3,
                                      GL_FLOAT,
                                      GL_FALSE,
                                      3 * sizeof(GLfloat),
                                      0);

                glBindBuffer(GL_ARRAY_BUFFER, font->vbo_colors);
                glEnableVertexAttribArray(f->attribute_color);

                if (f->dirty_display)
                        glBufferData(GL_ARRAY_BUFFER,
                                     font->num_glyphs_in_vbo * 6 * 3 * sizeof(GLfloat),
                                     font->colors,
                                     GL_DYNAMIC_DRAW);

                glVertexAttribPointer(f->attribute_color,
                                      3,
                                      GL_FLOAT,
                                      GL_FALSE,
                                      3 * sizeof(GLfloat),
                                      0);

                glActiveTexture(GL_TEXTURE0 + i);
                glBindTexture(GL_TEXTURE_2D, font->sprite_texture);
                glUniform1i(f->uniform_tex, i);
                glUniform1i(f->uniform_is_solid, 0);

                if (font->spritemap_dirty) {
                        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                        font->is_color_font ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

                        /* Upload the sprite map. */

                        glTexImage2D(GL_TEXTURE_2D,
                                     0,
                                     font->is_color_font ? GL_RGBA8 : GL_ALPHA,
                                     2048,
                                     2048,
                                     0,
                                     font->is_color_font ? GL_BGRA : GL_ALPHA,
                                     GL_UNSIGNED_BYTE,
                                     font->sprite_buffer);

                        if (font->is_color_font) glGenerateMipmap(GL_TEXTURE_2D);

                        font->spritemap_dirty = 0;
                }

                glUniform1i(f->uniform_is_color, !!font->is_color_font);

                glDrawArrays(GL_TRIANGLES, 0, font->num_glyphs_in_vbo * 6);
        }

        f->dirty_display = 0;
}
