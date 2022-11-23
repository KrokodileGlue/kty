#define _XOPEN_SOURCE 600

#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <inttypes.h>

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

int init_gl_resources(struct frame *f)
{
        /*
         * TODO: To support color fonts and stuff, each font must have its own
         * shader program. For now we're only dealing with colorless fonts so
         * we can use an alpha mask, so one shader program works for all.
         */

        const char vs[] = "#version 120\n\
attribute vec2 coord;\n\
attribute vec3 decoration_color;\n\
varying vec3 dec_color;\n\
void main(void) {\n\
        gl_Position = vec4(coord.xy, 0, 1);\n\
        dec_color = decoration_color;\n\
}";

        const char fs[] = "#version 120\n\
varying vec3 dec_color;\n\
uniform sampler2D tex;\n\
uniform vec4 color;\n\
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
                gl_FragColor = vec4(1, 1, 1, texture2D(tex, dec_color.xy).a) * color;\n\
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
        f->uniform_tex = bind_uniform_to_program(f->program, "tex");
        f->uniform_color = bind_uniform_to_program(f->program, "color");
        f->uniform_is_solid = bind_uniform_to_program(f->program, "is_solid");
        f->uniform_is_color = bind_uniform_to_program(f->program, "is_color");

        /* Enabling blending allows us to use alpha textures. */
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUniform4fv(f->uniform_color, 1, (GLfloat []){ 1, 1, 1, 1 });

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
                font = f->font.fonts + i;
                FT_Face face = font->face;
                FT_Set_Pixel_Sizes(face, 0, FONT_SIZE);

                uint32_t glyph_index = FT_Get_Char_Index(face, c);
                if (!glyph_index) continue;

                if (font->is_color_font) {
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
                        if (FT_Render_Glyph(face->glyph, font->render_mode)) continue;
                } else {
                        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) continue;
                }

                break;
        }

        if (!font) return NULL;

        FT_GlyphSlot slot = font->face->glyph;
        _printf("Adding sprite U+%x (%c) to font %s (%p)\n", c, c, font->path, slot->bitmap.buffer);

        if (!slot->bitmap.buffer) {
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

        _printf("-> %d,%d\n", x, y);
        _printf("(%d,%d)\n", cw, ch);

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
        float y = 1 - (f->w.ch * (1 + y0)) * sy - LINE_SPACING * y0 * sy - 0.25 * LINE_SPACING * sy;
        float x2 = x + metrics.horiBearingX * 1.0/64.0 * sx;
        float y2 = -y - sprite->bitmap_top * sy;
        float w = metrics.width * 1.0/64.0 * sx;
        float h = metrics.height * 1.0/64.0 * sy;

        if (w > 2 * f->w.cw * sx) w = 2 * f->w.cw * sx;
        if (h > f->w.ch * sy) {
                y2 = -y - f->w.ch * sy;
                h = (float)f->w.ch * sy + 0.5 * LINE_SPACING * sy;
                w = 2 * (float)f->w.cw * sx + 0.1 * LINE_SPACING * sx;
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
        } col[6] = {
                { sprite->tex_coords[0], sprite->tex_coords[1], 0 },
                { sprite->tex_coords[2], sprite->tex_coords[1], 0 },
                { sprite->tex_coords[0], sprite->tex_coords[3], 0 },

                { sprite->tex_coords[2], sprite->tex_coords[3], 0 },
                { sprite->tex_coords[2], sprite->tex_coords[1], 0 },
                { sprite->tex_coords[0], sprite->tex_coords[3], 0 },
        };

        struct font *font = sprite->font;

        memcpy(font->vertices + font->num_glyphs_in_vbo * sizeof box, box, sizeof box);
        memcpy(font->textures + font->num_glyphs_in_vbo * sizeof col, col, sizeof col);
        font->num_glyphs_in_vbo++;

        if (g.mode & GLYPH_UNDERLINE) {
                float w = x;
                float s = y - 3 * sy;
                float n = s + 1 * sy;
                float e = x + f->w.cw * sx;
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

                struct {
                        GLfloat r, g, b;
                } col[6] = {
                        { 1, 1, 1 },
                        { 1, 1, 1 },
                        { 1, 1, 1 },
                        { 1, 1, 1 },
                        { 1, 1, 1 },
                        { 1, 1, 1 },
                };

                memcpy(f->font.decoration + f->font.num_decoration * sizeof box,
                        box, sizeof box);
                memcpy(f->font.decoration_color + f->font.num_decoration * sizeof col,
                        col, sizeof col);
                f->font.num_decoration++;
        }

        return 0;
}

void render_cursor(struct frame *f)
{
        float sx = 2.0 / (float)f->w.width;
        float sy = 2.0 / (float)f->w.height;

        float w = -1 + (f->w.cw * f->c.x) * sx;
        float n = 1 - (f->w.ch * f->c.y) * sy - LINE_SPACING * f->c.y * sy;

        float s = n - f->w.ch * sy;
        float e = w + f->w.cw * sx;

        s -= LINE_SPACING * sy;

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

        struct {
                GLfloat r, g, b;
        } col[6] = {
                { 0, 0.5, 0.5 },
                { 0, 0.5, 0.5 },
                { 0, 0.5, 0.5 },
                { 0, 0.5, 0.5 },
                { 0, 0.5, 0.5 },
                { 0, 0.5, 0.5 },
        };

        memcpy(f->font.decoration + f->font.num_decoration * sizeof box,
                box, sizeof box);
        memcpy(f->font.decoration_color + f->font.num_decoration * sizeof col,
                col, sizeof col);
        f->font.num_decoration++;
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

                for (int i = 0; i < f->row; i++) {
                        for (int j = 0; j < f->col; j++) {
                                if (!f->line[i][j].c) continue;
                                //_printf("U+%x at %d,%d\n", f->line[i][j].c, j, i);
                                render_glyph(f, f->line[i][j], j, i);
                        }
                }
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

void free_resources(struct frame *f)
{
        glDeleteProgram(f->program);
}

int is_color_font(FT_Face face)
{
        static const uint32_t tag = FT_MAKE_TAG('C', 'B', 'D', 'T');
        unsigned long length = 0;
        FT_Load_Sfnt_Table(face, tag, 0, NULL, &length);
        return !!length;
}

int load_fonts(struct frame *f)
{
        /* TODO: Get fonts from command line options. */

        const char *path[] = {
                "SourceCodePro-Regular.otf",
                //"DejaVuSansMono.ttf",
                "NotoColorEmoji.ttf",
                "NotoSansCJK-Regular.ttc",
        };

        for (unsigned i = 0; i < sizeof path / sizeof *path; i++) {
                FT_Face face;

                if (FT_New_Face(f->font.ft, path[i], 0, &face)) {
                        fprintf(stderr, "Could not open font ‘%s’\n", path[i]);
                        return 1;
                }

                /*
                 * Hacky; this assumes that the first font in the list is the
                 * user's primary font and that the font is monospace.
                 */
                if (!i) {
                        FT_Set_Pixel_Sizes(face, 0, FONT_SIZE);

                        /*
                         * Whatever font is being used for an ASCII character
                         * like `x` is prooooobably the right font to base the
                         * grid size on.
                         */
                        FT_Load_Char(face, 'x', FT_LOAD_COMPUTE_METRICS);
                        FT_GlyphSlot slot = face->glyph;

                        /*
                         * We need to keep the width and height independent of
                         * font used to render non-ascii characters because we
                         * have to be able to treat the terminal like a grid.
                         */
                        f->w.cw = slot->metrics.horiAdvance / 64.0;
                        f->w.ch = slot->metrics.vertAdvance / 64.0;
                        _printf("%d,%d\n", f->w.cw, f->w.ch);
                }

                _printf("Loading font %s\n", path[i]);
                f->font.fonts[f->font.num_fonts++] = (struct font){
                        .path = path[i],
                        .face = face,
                        .is_color_font = is_color_font(face),
                        .render_mode = FT_RENDER_MODE_NORMAL,
                        .load_flags = 0,
                        /*
                         * TODO: Don't just hardcode this. The size obviously
                         * needs to fit inside of a single GPU texture...
                         */
                        .vertices = calloc(NUM_GLYPH * 2, 6 * 2 * sizeof(GLfloat)),
                        .textures = calloc(NUM_GLYPH * 2, 6 * 3 * sizeof(GLfloat)),
                        .sprite_buffer = calloc(1, 2048 * 2048 * 4),
                };

                /* Get a free VBO number. */
                glGenBuffers(1, &f->font.fonts[f->font.num_fonts - 1].vbo_vertices);
                glGenBuffers(1, &f->font.fonts[f->font.num_fonts - 1].vbo_textures);
                glGenTextures(1, &f->font.fonts[f->font.num_fonts - 1].sprite_texture);
        }

        /* Generate the underline VBO. */
        glGenBuffers(1, &f->font.vbo_decoration);
        f->font.decoration = calloc(NUM_GLYPH * 2, 6 * 2 * sizeof(GLfloat));
        glGenBuffers(1, &f->font.vbo_decoration_color);
        f->font.decoration_color = calloc(NUM_GLYPH * 2, 6 * 3 * sizeof(GLfloat));

        return 0;
}

struct frame *k;

void *read_shell(void *arg)
{
        (void)arg;
        struct frame *f = k;

        /* TODO: Put these into the frame. */
        static char buf[BUFSIZ];
        static int buflen = 0;
        int ret, written;

        while (1) {
                ret = read(f->master, buf + buflen, sizeof buf / sizeof *buf - buflen);

                switch (ret) {
                case 0:
                        exit(0);
                case -1:
                        fprintf(stderr, "Couldn't read from shell\n");
                        exit(1);
                default:
                        buflen += ret;
                        written = twrite(f, buf, buflen);
                        buflen -= written;
                        if (buflen > 0)
                                memmove(buf, buf + written, buflen);
                }
        }

        f->shell_done = true;

        return NULL;
}

void character_callback(GLFWwindow *window, uint32_t c)
{
        (void)window;
        uint8_t buf[4];
        unsigned len = 0;
        utf8encode(c, buf, &len);
        write(k->master, buf, len);
}

void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
        (void)window, (void)mods, (void)scancode;

        if (action != GLFW_PRESS) return;

        /*
         * The GLFW keycode things are hardcoded
         * (https://www.glfw.org/docs/3.3/group__keys.html), so it's safe to
         * use this conditional.
         */
        if (key >= 'A' && key <= 'Z' && mods & GLFW_MOD_CONTROL) {
                write(k->master, (char []){ key - 'A' + 1 }, 1);
                return;
        }

        switch (key) {
        case GLFW_KEY_ENTER:
                write(k->master, "\n", 1);
                break;
        case GLFW_KEY_BACKSPACE:
                write(k->master, "\b", 1);
                break;
        case GLFW_KEY_ESCAPE:
                write(k->master, "\x1b", 1);
                break;
        case GLFW_KEY_TAB:
                write(k->master, "\t", 1);
                break;
        case GLFW_KEY_LEFT:
                write(k->master, "\x1b[D", 3);
                break;
        case GLFW_KEY_RIGHT:
                write(k->master, "\x1b[C", 3);
                break;
        case GLFW_KEY_UP:
                write(k->master, "\x1b[A", 3);
                break;
        case GLFW_KEY_DOWN:
                write(k->master, "\x1b[B", 3);
                break;
        }
}

void window_size_callback(GLFWwindow *window, int width, int height)
{
        /*
         * TODO: Obviously to support multiple frames in a single window this
         * will need to be generalized so that it updates all of the frames.
         * That's a relatively complex tiling window manager type of operation,
         * so for now I'll just assume one frame.
         */

        (void)window;
        k->w.width = width, k->w.height = height;
        tresize(k, width / k->w.cw, height / (k->w.ch + LINE_SPACING));
        glViewport(0, 0, width, height);

        struct winsize ws = {
                .ws_col = k->col,
                .ws_row = k->row,
        };

        if (ioctl(k->master, TIOCSWINSZ, &ws) == -1)
                perror("ioctl");
}

int main(int, char **, char **env)
{
        /* Set up the PTY. */
        int master = posix_openpt(O_RDWR | O_NOCTTY);

        if (master == -1) {
                perror("posix_openpt");
                return 1;
        }

        if (grantpt(master) == -1) {
                perror("grantpt");
                return 1;
        }

        if (unlockpt(master) == -1) {
                perror("unlockpt");
                return 1;
        }

        const char *slave_name = ptsname(master);

        if (!slave_name) {
                perror("ptsname");
                return 1;
        }

        puts(slave_name);

        int slave = open(slave_name, O_RDWR | O_NOCTTY);

        if (slave == -1) {
                perror("open");
                return 1;
        }

        int p = fork();

        if (!p) {
                close(master);

                setsid();
                if (ioctl(slave, TIOCSCTTY, NULL) == -1) {
                        perror("ioctl");
                        return 1;
                }

                dup2(slave, 0);
                dup2(slave, 1);
                dup2(slave, 2);
                close(slave);

                execle("/bin/bash", "/bin/bash", NULL, env);
        } else {
                close(slave);
        }

        /* The shell is running, now set up the window/graphics. */
        k = calloc(sizeof *k, 1);
        if (!k) return 1;

        k->master = master;
        k->mode = MODE_CURSOR_VISIBLE;

        if (!glfwInit()) return 1;

        GLFWwindow *window = glfwCreateWindow(
                WINDOW_WIDTH,
                WINDOW_HEIGHT,
                "Hello, world!",
                NULL,
                NULL
        );

        if (!window) {
                glfwTerminate();
                return 1;
        }

        glfwMakeContextCurrent(window);

        glfwSetCharCallback(window, character_callback);
        glfwSetKeyCallback(window, key_callback);
        glfwSetWindowSizeCallback(window, window_size_callback);

        /* Initialize FreeType. */
        if (FT_Init_FreeType(&k->font.ft)) {
                fprintf(stderr, "Could not init FreeType\n");
                return 1;
        }

        /* Initialize GLEW. */
        GLenum glew_status = glewInit();

        if (GLEW_OK != glew_status) {
                fprintf(stderr, "Error: %s\n", glewGetErrorString(glew_status));
                return 1;
        }

        if (!GLEW_VERSION_2_0) {
                fprintf(stderr, "No support for OpenGL 2.0 found\n");
                return 1;
        }

        /* Load fonts. */
        if (load_fonts(k)) return 1;

        int width, height;
        glfwGetWindowSize(window, &width, &height);
        window_size_callback(window, width, height);

        /*
         * Right now these are being set after the resize callback because the
         * resize callback is what sets `k->row`. TODO: Move the frame
         * initialization stuff to a separate function.
         */
        k->top = 0;
        k->bot = k->row - 1;

        /* Create the VBO, shader program, etc. */
        if (init_gl_resources(k)) return 1;

        pthread_t shell_reader;
        pthread_create(&shell_reader, NULL, read_shell, k);

        render(k);
        glfwSwapBuffers(window);

        while (!glfwWindowShouldClose(window) && !k->shell_done) {
                render(k);
                glfwSwapBuffers(window);
                glfwPollEvents();
        }

        free_resources(k);
        glfwTerminate();

        return 0;
}
