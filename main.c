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
        _printf("Adding sprite U+%x (%s) to font %s\n", c, buf, font->path);
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
        float y = 1 - (f->w.ch * (1 + y0)) * sy - LINE_SPACING * y0 * sy - 0.25 * LINE_SPACING * sy;
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
                "NotoColorEmoji.ttf",
                "NotoSansCJK-Regular.ttc",
                "TibMachUni-1.901b.ttf",
                "DejaVuSansMono.ttf",
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
                        .colors = calloc(NUM_GLYPH * 2, 6 * 3 * sizeof(GLfloat)),
                        .sprite_buffer = calloc(1, 2048 * 2048 * 4),
                };

                /* Get a free VBO number. */
                glGenBuffers(1, &f->font.fonts[f->font.num_fonts - 1].vbo_vertices);
                glGenBuffers(1, &f->font.fonts[f->font.num_fonts - 1].vbo_textures);
                glGenBuffers(1, &f->font.fonts[f->font.num_fonts - 1].vbo_colors);

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

static struct key {
        int keysym, mask;
        char *s;
        int appkey, appcursor;
} keys[] = {
        /* keysym           mask            string      appkey appcursor */
        /* { GLFW_KEY_KP_HOME,       GLFW_MOD_SHIFT,      "\033[2J",       0,   -1}, */
        /* { GLFW_KEY_KP_HOME,       GLFW_MOD_SHIFT,      "\033[1;2H",     0,   +1}, */
        /* { GLFW_KEY_KP_HOME,       0,              "\033[H",        0,   -1}, */
        /* { GLFW_KEY_KP_HOME,       0,              "\033[1~",       0,   +1}, */
        /* { GLFW_KEY_KP_UP,         0,              "\033Ox",       +1,    0}, */
        /* { GLFW_KEY_KP_UP,         0,              "\033[A",        0,   -1}, */
        /* { GLFW_KEY_KP_UP,         0,              "\033OA",        0,   +1}, */
        /* { GLFW_KEY_KP_DOWN,       0,              "\033Or",       +1,    0}, */
        /* { GLFW_KEY_KP_DOWN,       0,              "\033[B",        0,   -1}, */
        /* { GLFW_KEY_KP_DOWN,       0,              "\033OB",        0,   +1}, */
        /* { GLFW_KEY_KP_LEFT,       0,              "\033Ot",       +1,    0}, */
        /* { GLFW_KEY_KP_LEFT,       0,              "\033[D",        0,   -1}, */
        /* { GLFW_KEY_KP_LEFT,       0,              "\033OD",        0,   +1}, */
        /* { GLFW_KEY_KP_RIGHT,      0,              "\033Ov",       +1,    0}, */
        /* { GLFW_KEY_KP_RIGHT,      0,              "\033[C",        0,   -1}, */
        /* { GLFW_KEY_KP_RIGHT,      0,              "\033OC",        0,   +1}, */
        /* { GLFW_KEY_KP_PRIOR,      GLFW_MOD_SHIFT,      "\033[5;2~",     0,    0}, */
        /* { GLFW_KEY_KP_PRIOR,      0,              "\033[5~",       0,    0}, */
        /* { GLFW_KEY_KP_BEGIN,      0,              "\033[E",        0,    0}, */
        /* { GLFW_KEY_KP_END,        GLFW_MOD_CONTROL,    "\033[J",       -1,    0}, */
        /* { GLFW_KEY_KP_END,        GLFW_MOD_CONTROL,    "\033[1;5F",    +1,    0}, */
        /* { GLFW_KEY_KP_END,        GLFW_MOD_SHIFT,      "\033[K",       -1,    0}, */
        /* { GLFW_KEY_KP_END,        GLFW_MOD_SHIFT,      "\033[1;2F",    +1,    0}, */
        /* { GLFW_KEY_KP_END,        0,              "\033[4~",       0,    0}, */
        /* { GLFW_KEY_KP_NEXT,       GLFW_MOD_SHIFT,      "\033[6;2~",     0,    0}, */
        /* { GLFW_KEY_KP_NEXT,       0,              "\033[6~",       0,    0}, */
        /* { GLFW_KEY_KP_INSERT,     GLFW_MOD_SHIFT,      "\033[2;2~",    +1,    0}, */
        /* { GLFW_KEY_KP_INSERT,     GLFW_MOD_SHIFT,      "\033[4l",      -1,    0}, */
        /* { GLFW_KEY_KP_INSERT,     GLFW_MOD_CONTROL,    "\033[L",       -1,    0}, */
        /* { GLFW_KEY_KP_INSERT,     GLFW_MOD_CONTROL,    "\033[2;5~",    +1,    0}, */
        /* { GLFW_KEY_KP_INSERT,     0,              "\033[4h",      -1,    0}, */
        /* { GLFW_KEY_KP_INSERT,     0,              "\033[2~",      +1,    0}, */
        /* { GLFW_KEY_KP_DELETE,     GLFW_MOD_CONTROL,    "\033[M",       -1,    0}, */
        /* { GLFW_KEY_KP_DELETE,     GLFW_MOD_CONTROL,    "\033[3;5~",    +1,    0}, */
        /* { GLFW_KEY_KP_DELETE,     GLFW_MOD_SHIFT,      "\033[2K",      -1,    0}, */
        /* { GLFW_KEY_KP_DELETE,     GLFW_MOD_SHIFT,      "\033[3;2~",    +1,    0}, */
        /* { GLFW_KEY_KP_DELETE,     0,              "\033[P",       -1,    0}, */
        /* { GLFW_KEY_KP_DELETE,     0,              "\033[3~",      +1,    0}, */
        { GLFW_KEY_KP_MULTIPLY,   0,              "\033Oj",       +2,    0},
        { GLFW_KEY_KP_ADD,        0,              "\033Ok",       +2,    0},
        { GLFW_KEY_KP_ENTER,      0,              "\033OM",       +2,    0},
        { GLFW_KEY_KP_ENTER,      0,              "\r",           -1,    0},
        { GLFW_KEY_KP_SUBTRACT,   0,              "\033Om",       +2,    0},
        { GLFW_KEY_KP_DECIMAL,    0,              "\033On",       +2,    0},
        { GLFW_KEY_KP_DIVIDE,     0,              "\033Oo",       +2,    0},
        { GLFW_KEY_KP_0,          0,              "\033Op",       +2,    0},
        { GLFW_KEY_KP_1,          0,              "\033Oq",       +2,    0},
        { GLFW_KEY_KP_2,          0,              "\033Or",       +2,    0},
        { GLFW_KEY_KP_3,          0,              "\033Os",       +2,    0},
        { GLFW_KEY_KP_4,          0,              "\033Ot",       +2,    0},
        { GLFW_KEY_KP_5,          0,              "\033Ou",       +2,    0},
        { GLFW_KEY_KP_6,          0,              "\033Ov",       +2,    0},
        { GLFW_KEY_KP_7,          0,              "\033Ow",       +2,    0},
        { GLFW_KEY_KP_8,          0,              "\033Ox",       +2,    0},
        { GLFW_KEY_KP_9,          0,              "\033Oy",       +2,    0},

        { GLFW_KEY_UP,          GLFW_MOD_SHIFT,                               "\033[1;2A", 0,  0  },
        { GLFW_KEY_UP,          GLFW_MOD_ALT,                                 "\033[1;3A", 0,  0  },
        { GLFW_KEY_UP,          GLFW_MOD_SHIFT|GLFW_MOD_ALT,                  "\033[1;4A", 0,  0  },
        { GLFW_KEY_UP,          GLFW_MOD_CONTROL,                             "\033[1;5A", 0,  0  },
        { GLFW_KEY_UP,          GLFW_MOD_SHIFT|GLFW_MOD_CONTROL,              "\033[1;6A", 0,  0  },
        { GLFW_KEY_UP,          GLFW_MOD_CONTROL|GLFW_MOD_ALT,                "\033[1;7A", 0,  0  },
        { GLFW_KEY_UP,          GLFW_MOD_SHIFT|GLFW_MOD_CONTROL|GLFW_MOD_ALT, "\033[1;8A", 0,  0  },
        { GLFW_KEY_UP,          0,                                            "\033[A",    0,  -1 },
        { GLFW_KEY_UP,          0,                                            "\033OA",    0,  +1 },
        { GLFW_KEY_DOWN,        GLFW_MOD_SHIFT,                               "\033[1;2B", 0,  0  },
        { GLFW_KEY_DOWN,        GLFW_MOD_ALT,                                 "\033[1;3B", 0,  0  },
        { GLFW_KEY_DOWN,        GLFW_MOD_SHIFT|GLFW_MOD_ALT,                  "\033[1;4B", 0,  0  },
        { GLFW_KEY_DOWN,        GLFW_MOD_CONTROL,                             "\033[1;5B", 0,  0  },
        { GLFW_KEY_DOWN,        GLFW_MOD_SHIFT|GLFW_MOD_CONTROL,              "\033[1;6B", 0,  0  },
        { GLFW_KEY_DOWN,        GLFW_MOD_CONTROL|GLFW_MOD_ALT,                "\033[1;7B", 0,  0  },
        { GLFW_KEY_DOWN,        GLFW_MOD_SHIFT|GLFW_MOD_CONTROL|GLFW_MOD_ALT, "\033[1;8B", 0,  0  },
        { GLFW_KEY_DOWN,        0,                                            "\033[B",    0,  -1 },
        { GLFW_KEY_DOWN,        0,                                            "\033OB",    0,  +1 },
        { GLFW_KEY_LEFT,        GLFW_MOD_SHIFT,                               "\033[1;2D", 0,  0  },
        { GLFW_KEY_LEFT,        GLFW_MOD_ALT,                                 "\033[1;3D", 0,  0  },
        { GLFW_KEY_LEFT,        GLFW_MOD_SHIFT|GLFW_MOD_ALT,                  "\033[1;4D", 0,  0  },
        { GLFW_KEY_LEFT,        GLFW_MOD_CONTROL,                             "\033[1;5D", 0,  0  },
        { GLFW_KEY_LEFT,        GLFW_MOD_SHIFT|GLFW_MOD_CONTROL,              "\033[1;6D", 0,  0  },
        { GLFW_KEY_LEFT,        GLFW_MOD_CONTROL|GLFW_MOD_ALT,                "\033[1;7D", 0,  0  },
        { GLFW_KEY_LEFT,        GLFW_MOD_SHIFT|GLFW_MOD_CONTROL|GLFW_MOD_ALT, "\033[1;8D", 0,  0  },
        { GLFW_KEY_LEFT,        0,                                            "\033[D",    0,  -1 },
        { GLFW_KEY_LEFT,        0,                                            "\033OD",    0,  +1 },
        { GLFW_KEY_RIGHT,       GLFW_MOD_SHIFT,                               "\033[1;2C", 0,  0  },
        { GLFW_KEY_RIGHT,       GLFW_MOD_ALT,                                 "\033[1;3C", 0,  0  },
        { GLFW_KEY_RIGHT,       GLFW_MOD_SHIFT|GLFW_MOD_ALT,                  "\033[1;4C", 0,  0  },
        { GLFW_KEY_RIGHT,       GLFW_MOD_CONTROL,                             "\033[1;5C", 0,  0  },
        { GLFW_KEY_RIGHT,       GLFW_MOD_SHIFT|GLFW_MOD_CONTROL,              "\033[1;6C", 0,  0  },
        { GLFW_KEY_RIGHT,       GLFW_MOD_CONTROL|GLFW_MOD_ALT,                "\033[1;7C", 0,  0  },
        { GLFW_KEY_RIGHT,       GLFW_MOD_SHIFT|GLFW_MOD_CONTROL|GLFW_MOD_ALT, "\033[1;8C", 0,  0  },
        { GLFW_KEY_RIGHT,       0,                                            "\033[C",    0,  -1 },
        { GLFW_KEY_RIGHT,       0,                                            "\033OC",    0,  +1 },
        /* { GLFW_KEY_ISO_Left_Tab,GLFW_MOD_SHIFT,                               "\033[Z",    0,  0  }, */
        { GLFW_KEY_ENTER,       GLFW_MOD_ALT,                                 "\033\r",    0,  0  },
        { GLFW_KEY_ENTER,       0,                                            "\r",        0,  0  },
        { GLFW_KEY_INSERT,      GLFW_MOD_SHIFT,                               "\033[4l",   -1, 0  },
        { GLFW_KEY_INSERT,      GLFW_MOD_SHIFT,                               "\033[2;2~", +1, 0  },
        { GLFW_KEY_INSERT,      GLFW_MOD_CONTROL,                             "\033[L",    -1, 0  },
        { GLFW_KEY_INSERT,      GLFW_MOD_CONTROL,                             "\033[2;5~", +1, 0  },
        { GLFW_KEY_INSERT,      0,                                            "\033[4h",   -1, 0  },
        { GLFW_KEY_INSERT,      0,                                            "\033[2~",   +1, 0  },
        { GLFW_KEY_DELETE,      GLFW_MOD_CONTROL,                             "\033[M",    -1, 0  },
        { GLFW_KEY_DELETE,      GLFW_MOD_CONTROL,                             "\033[3;5~", +1, 0  },
        { GLFW_KEY_DELETE,      GLFW_MOD_SHIFT,                               "\033[2K",   -1, 0  },
        { GLFW_KEY_DELETE,      GLFW_MOD_SHIFT,                               "\033[3;2~", +1, 0  },
        { GLFW_KEY_DELETE,      0,                                            "\033[P",    -1, 0  },
        { GLFW_KEY_DELETE,      0,                                            "\033[3~",   +1, 0  },
        { GLFW_KEY_BACKSPACE,   0,                                            "\177",      0,  0  },
        { GLFW_KEY_BACKSPACE,   GLFW_MOD_ALT,                                 "\033\177",  0,  0  },
        { GLFW_KEY_HOME,        GLFW_MOD_ALT,                                 "\033[1;3H", 0,  -1 },
        { GLFW_KEY_HOME,        GLFW_MOD_SHIFT,                               "\033[2J",   0,  -1 },
        { GLFW_KEY_HOME,        GLFW_MOD_SHIFT,                               "\033[1;2H", 0,  +1 },
        { GLFW_KEY_HOME,        0,                                            "\033[H",    0,  -1 },
        { GLFW_KEY_HOME,        0,                                            "\033[1~",   0,  +1 },
        { GLFW_KEY_END,         GLFW_MOD_ALT,                                 "\033[1;3F", 0,  -1 },
        { GLFW_KEY_END,         GLFW_MOD_CONTROL,                             "\033[J",    -1, 0  },
        { GLFW_KEY_END,         GLFW_MOD_CONTROL,                             "\033[1;5F", +1, 0  },
        { GLFW_KEY_END,         GLFW_MOD_SHIFT,                               "\033[K",    -1, 0  },
        { GLFW_KEY_END,         GLFW_MOD_SHIFT,                               "\033[1;2F", +1, 0  },
        { GLFW_KEY_END,         0,                                            "\033[4~",   0,  0  },
        { GLFW_KEY_PAGE_UP,     GLFW_MOD_CONTROL,                             "\033[5;5~", 0,  0  },
        { GLFW_KEY_PAGE_UP,     GLFW_MOD_SHIFT,                               "\033[5;2~", 0,  0  },
        { GLFW_KEY_PAGE_UP,     0,                                            "\033[5~",   0,  0  },
        { GLFW_KEY_PAGE_DOWN,   GLFW_MOD_CONTROL,                             "\033[6;5~", 0,  0  },
        { GLFW_KEY_PAGE_DOWN,   GLFW_MOD_SHIFT,                               "\033[6;2~", 0,  0  },
        { GLFW_KEY_PAGE_DOWN,   0,                                            "\033[6~",   0,  0  },

        { GLFW_KEY_F1,                   0,       "\033OP" ,       0,    0},
        { GLFW_KEY_F1, /* F13 */  GLFW_MOD_SHIFT,      "\033[1;2P",     0,    0},
        { GLFW_KEY_F1, /* F25 */  GLFW_MOD_CONTROL,    "\033[1;5P",     0,    0},
        { GLFW_KEY_F1, /* F37 */  GLFW_MOD_SUPER,       "\033[1;6P",     0,    0},
        { GLFW_KEY_F1, /* F49 */  GLFW_MOD_ALT,       "\033[1;3P",     0,    0},
        /* { GLFW_KEY_F1, /\* F61 *\/  GLFW_MOD_MOD3,       "\033[1;4P",     0,    0}, */
        { GLFW_KEY_F2,                   0,       "\033OQ" ,       0,    0},
        { GLFW_KEY_F2, /* F14 */  GLFW_MOD_SHIFT,      "\033[1;2Q",     0,    0},
        { GLFW_KEY_F2, /* F26 */  GLFW_MOD_CONTROL,    "\033[1;5Q",     0,    0},
        { GLFW_KEY_F2, /* F38 */  GLFW_MOD_SUPER,       "\033[1;6Q",     0,    0},
        { GLFW_KEY_F2, /* F50 */  GLFW_MOD_ALT,       "\033[1;3Q",     0,    0},
        /* { GLFW_KEY_F2, /\* F62 *\/  GLFW_MOD_MOD3,       "\033[1;4Q",     0,    0}, */
        { GLFW_KEY_F3,                   0,       "\033OR" ,       0,    0},
        { GLFW_KEY_F3, /* F15 */  GLFW_MOD_SHIFT,      "\033[1;2R",     0,    0},
        { GLFW_KEY_F3, /* F27 */  GLFW_MOD_CONTROL,    "\033[1;5R",     0,    0},
        { GLFW_KEY_F3, /* F39 */  GLFW_MOD_SUPER,       "\033[1;6R",     0,    0},
        { GLFW_KEY_F3, /* F51 */  GLFW_MOD_ALT,       "\033[1;3R",     0,    0},
        /* { GLFW_KEY_F3, /\* F63 *\/  GLFW_MOD_MOD3,       "\033[1;4R",     0,    0}, */
        { GLFW_KEY_F4,                   0,       "\033OS" ,       0,    0},
        { GLFW_KEY_F4, /* F16 */  GLFW_MOD_SHIFT,      "\033[1;2S",     0,    0},
        { GLFW_KEY_F4, /* F28 */  GLFW_MOD_CONTROL,    "\033[1;5S",     0,    0},
        { GLFW_KEY_F4, /* F40 */  GLFW_MOD_SUPER,       "\033[1;6S",     0,    0},
        { GLFW_KEY_F4, /* F52 */  GLFW_MOD_ALT,       "\033[1;3S",     0,    0},
        { GLFW_KEY_F5,                   0,       "\033[15~",      0,    0},
        { GLFW_KEY_F5, /* F17 */  GLFW_MOD_SHIFT,      "\033[15;2~",    0,    0},
        { GLFW_KEY_F5, /* F29 */  GLFW_MOD_CONTROL,    "\033[15;5~",    0,    0},
        { GLFW_KEY_F5, /* F41 */  GLFW_MOD_SUPER,       "\033[15;6~",    0,    0},
        { GLFW_KEY_F5, /* F53 */  GLFW_MOD_ALT,       "\033[15;3~",    0,    0},
        { GLFW_KEY_F6,                   0,       "\033[17~",      0,    0},
        { GLFW_KEY_F6, /* F18 */  GLFW_MOD_SHIFT,      "\033[17;2~",    0,    0},
        { GLFW_KEY_F6, /* F30 */  GLFW_MOD_CONTROL,    "\033[17;5~",    0,    0},
        { GLFW_KEY_F6, /* F42 */  GLFW_MOD_SUPER,       "\033[17;6~",    0,    0},
        { GLFW_KEY_F6, /* F54 */  GLFW_MOD_ALT,       "\033[17;3~",    0,    0},
        { GLFW_KEY_F7,                   0,       "\033[18~",      0,    0},
        { GLFW_KEY_F7, /* F19 */  GLFW_MOD_SHIFT,      "\033[18;2~",    0,    0},
        { GLFW_KEY_F7, /* F31 */  GLFW_MOD_CONTROL,    "\033[18;5~",    0,    0},
        { GLFW_KEY_F7, /* F43 */  GLFW_MOD_SUPER,       "\033[18;6~",    0,    0},
        { GLFW_KEY_F7, /* F55 */  GLFW_MOD_ALT,       "\033[18;3~",    0,    0},
        { GLFW_KEY_F8,                   0,       "\033[19~",      0,    0},
        { GLFW_KEY_F8, /* F20 */  GLFW_MOD_SHIFT,      "\033[19;2~",    0,    0},
        { GLFW_KEY_F8, /* F32 */  GLFW_MOD_CONTROL,    "\033[19;5~",    0,    0},
        { GLFW_KEY_F8, /* F44 */  GLFW_MOD_SUPER,       "\033[19;6~",    0,    0},
        { GLFW_KEY_F8, /* F56 */  GLFW_MOD_ALT,       "\033[19;3~",    0,    0},
        { GLFW_KEY_F9,                   0,       "\033[20~",      0,    0},
        { GLFW_KEY_F9, /* F21 */  GLFW_MOD_SHIFT,      "\033[20;2~",    0,    0},
        { GLFW_KEY_F9, /* F33 */  GLFW_MOD_CONTROL,    "\033[20;5~",    0,    0},
        { GLFW_KEY_F9, /* F45 */  GLFW_MOD_SUPER,       "\033[20;6~",    0,    0},
        { GLFW_KEY_F9, /* F57 */  GLFW_MOD_ALT,       "\033[20;3~",    0,    0},
        { GLFW_KEY_F10,                  0,       "\033[21~",      0,    0},
        { GLFW_KEY_F10, /* F22 */ GLFW_MOD_SHIFT,      "\033[21;2~",    0,    0},
        { GLFW_KEY_F10, /* F34 */ GLFW_MOD_CONTROL,    "\033[21;5~",    0,    0},
        { GLFW_KEY_F10, /* F46 */ GLFW_MOD_SUPER,       "\033[21;6~",    0,    0},
        { GLFW_KEY_F10, /* F58 */ GLFW_MOD_ALT,       "\033[21;3~",    0,    0},
        { GLFW_KEY_F11,                  0,       "\033[23~",      0,    0},
        { GLFW_KEY_F11, /* F23 */ GLFW_MOD_SHIFT,      "\033[23;2~",    0,    0},
        { GLFW_KEY_F11, /* F35 */ GLFW_MOD_CONTROL,    "\033[23;5~",    0,    0},
        { GLFW_KEY_F11, /* F47 */ GLFW_MOD_SUPER,       "\033[23;6~",    0,    0},
        { GLFW_KEY_F11, /* F59 */ GLFW_MOD_ALT,       "\033[23;3~",    0,    0},
        { GLFW_KEY_F12,                  0,       "\033[24~",      0,    0},
        { GLFW_KEY_F12, /* F24 */ GLFW_MOD_SHIFT,      "\033[24;2~",    0,    0},
        { GLFW_KEY_F12, /* F36 */ GLFW_MOD_CONTROL,    "\033[24;5~",    0,    0},
        { GLFW_KEY_F12, /* F48 */ GLFW_MOD_SUPER,       "\033[24;6~",    0,    0},
        { GLFW_KEY_F12, /* F60 */ GLFW_MOD_ALT,       "\033[24;3~",    0,    0},
};

void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
        (void)window, (void)mods, (void)scancode;

        if (action == GLFW_RELEASE) return;

        /*
         * The GLFW keycode things are hardcoded
         * (https://www.glfw.org/docs/3.3/group__keys.html), so it's
         * safe to use these conditionals.
         */

        if (key >= 'A' && key <= 'Z' && mods & GLFW_MOD_CONTROL) {
                write(k->master, (char []){ key - 'A' + 1 }, 1);
                return;
        }

        if (key >= 32 && key <= 126 && mods & GLFW_MOD_ALT) {
                write(k->master, (char []){ 0x1b, tolower(key) }, 2);
                return;
        }

        const char *s = NULL;

        for (unsigned i = 0; i < sizeof keys / sizeof *keys; i++) {
                if (key != keys[i].keysym) continue;
                if ((mods & keys[i].mask) != keys[i].mask) continue;

                if (!keys[i].appcursor) {
                        s = keys[i].s;
                        break;
                }

                if (keys[i].appcursor > 0 && !(k->mode & MODE_APPCURSOR))
                        continue;

                if (keys[i].appcursor < 0 && (k->mode & MODE_APPCURSOR))
                        continue;

                s = keys[i].s;
                break;
        }

        if (s) write(k->master, s, strlen(s));
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
