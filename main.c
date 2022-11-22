#define _XOPEN_SOURCE 600

#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <inttypes.h>

#include <unistd.h>
#include <pthread.h> /* TODO: Windows support. */
#include <fcntl.h>
#include <sys/ioctl.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <freetype/tttables.h>
#include <GL/glew.h>
#include <GL/gl.h>

#include <GLFW/glfw3.h>

/* No one will ever need more than 16 fonts. */
#define MAX_FONTS 16
#define WINDOW_WIDTH 600
#define WINDOW_HEIGHT 700
#define FONT_SIZE 12
#define NUM_GLYPH 2000
#define LINE_SPACING 4

/* How long could an escape sequence possibly be. */
#define ESC_ARG_SIZE 512

#define ISCONTROLC0(c) ((0 < c && c < 0x1f) || (c) == 0x7f)
#define ISCONTROLC1(c) (0x80 < c && c < 0x9f)
#define ISCONTROL(c) (ISCONTROLC0(c) || ISCONTROLC1(c))

void dprintf(const char *func, const char *fmt, ...)
{
        va_list args;
        va_start(args, fmt);
        struct tm *t = localtime(&(time_t){time(NULL)});
        char buf[BUFSIZ];
        strftime(buf, sizeof buf, "%M:%S", t);
        fprintf(stderr, "%s ", buf);
        vfprintf(stderr, fmt, args);
        va_end(args);
}

#define dprintf(...) dprintf(__func__, __VA_ARGS__)

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
        GLuint vbo_vertices;
        GLuint vbo_textures;
        int num_glyphs_in_vbo;

        int num_glyph;              /* The number of glyphs in the spritemap. */

        int pixel_size;
        int load_flags;
};

enum {
        GLYPH_WRAP = 1,
        GLYPH_UNDERLINE = 1 << 1,
        GLYPH_BOLD = 1 << 2,
        GLYPH_ITALIC = 1 << 3,
        GLYPH_DIM = 1 << 4,
        GLYPH_BLINKING = 1 << 5,
        GLYPH_INVERSE = 1 << 5,
        GLYPH_MAX = 1 << 7,
};

struct glyph {
        uint32_t c;
        int mode;
};

enum {
        CURSOR_DEFAULT = 0,
        CURSOR_ORIGIN = 1,
        CURSOR_WRAPNEXT = 1 << 1,
};

struct cursor {
        int x, y, mode, state;
};

struct window {
        int width, height;
        int cw, ch;
};

struct font_renderer {
        /* FreeType */
        FT_Library ft;

        /* TODO: Use a hashtable for this. */
        struct sprite {
                uint32_t c;
                struct font *font;
                FT_Glyph_Metrics metrics;
                int bitmap_top;
                float tex_coords[4];
        } glyph[NUM_GLYPH];
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
        GLint uniform_tex;
        GLint uniform_color;
        GLint uniform_is_solid;

        /* State */
        struct window w;
        int col, row;
        int top, bot; /* Required for tsetscroll */
        struct cursor c;
        struct glyph **line;

        bool shell_done; /* bazinga */

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
};

int print_gl_error_log(GLuint object)
{
        GLint log_length;

        if (glIsShader(object)) {
                glGetShaderiv(object, GL_INFO_LOG_LENGTH, &log_length);
        } else if (glIsProgram(object)) {
                glGetProgramiv(object, GL_INFO_LOG_LENGTH, &log_length);
        } else {
                return 1;
        }

        char *log = malloc(log_length);
        if (!log) return 1;

        if (glIsShader(object)) {
                glGetShaderInfoLog(object, log_length, NULL, log);
        } else if (glIsProgram(object)) {
                glGetProgramInfoLog(object, log_length, NULL, log);
        }

        fprintf(stderr, "%s", log);
        free(log);

        return 0;
}

GLint create_shader(const GLchar *source, GLenum type)
{
        GLuint res = glCreateShader(type);
        glShaderSource(res, 1, &source, NULL);

        glCompileShader(res);
        GLint compile_ok = GL_FALSE;
        glGetShaderiv(res, GL_COMPILE_STATUS, &compile_ok);
        if (compile_ok == GL_FALSE) {
                print_gl_error_log(res);
                glDeleteShader(res);
                return 0;
        }

        return res;
}

int bind_uniform_to_program(GLuint program, const char *name)
{
        int coord = glGetUniformLocation(program, name);

        if (coord < 0) {
                fprintf(stderr, "Couldn't bind uniform %s\n", name);
                exit(1);
        }

        return coord;
}

int bind_attribute_to_program(GLuint program, const char *name)
{
        int coord = glGetAttribLocation(program, name);

        if (coord < 0) {
                fprintf(stderr, "Couldn't bind attribute %s\n", name);
                exit(1);
        }

        return coord;
}

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
void main(void) {\n\
        gl_FragColor = is_solid == 1 ? vec4(dec_color, 1) : vec4(1, 1, 1, texture2D(tex, dec_color.xy).a) * color;\n\
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
        //printf("Adding sprite U+%x (%c) to font %s (%p)\n", c, c, font->path, slot->bitmap.buffer);

        if (!slot->bitmap.buffer) {
                f->font.glyph[f->font.num_glyph] = (struct sprite){
                        .c = c,
                        .metrics = slot->metrics,
                        .bitmap_top = slot->bitmap_top,
                        .tex_coords = { 0, 0, 0.5, 1 },
                        .font = font,
                };
                font->num_glyph++;

                return &f->font.glyph[f->font.num_glyph++];
        }

        int cw = 10, ch = 10; /* TODO: Fix this haha. */
        int sw = 2048 / cw;
        int x = (font->num_glyph % sw) * cw;
        int y = (font->num_glyph / sw) * ch;

        //printf("-> %d,%d\n", x, y);
        //printf("%d,%d,%d,%d\n", slot->bitmap.rows, slot->bitmap.width, ch, cw);

        /* Write the sprite into the spritemap. */
        for (unsigned i = 0; i < slot->bitmap.rows; i++)
                memcpy(font->sprite_buffer + (y + i) * 2048 + x,
                        slot->bitmap.buffer + (i * slot->bitmap.width),
                        slot->bitmap.width);

        float fx0 = (float)x / 2048.0;
        float fx1 = (float)(x + slot->bitmap.width) / 2048.0;
        float fy0 = (float)y / 2048.0;
        float fy1 = (float)(y + slot->bitmap.rows) / 2048.0;

        f->font.glyph[f->font.num_glyph] = (struct sprite){
                .c = c,
                .metrics = slot->metrics,
                .bitmap_top = slot->bitmap_top,
                .tex_coords = { fx0, fy0, fx1, fy1 },
                .font = font,
        };

        //printf("[%f, %f]\n", fx0, fy0);
        //printf("[%f, %f]\n", fx1, fy0);
        //printf("[%f, %f]\n", fx0, fy1);
        //printf("[%f, %f]\n", fx1, fy1);

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

        /* Calculate the vertex and texture coordinates */
        float x = -1 + (f->w.cw * x0) * sx;
        float y = 1 - (f->w.ch * (1 + y0)) * sy - LINE_SPACING * y0 * sy;
        float x2 = x + metrics.horiBearingX * 1.0/64.0 * sx;
        float y2 = -y - sprite->bitmap_top * sy;
        float w = metrics.width * 1.0/64.0 * sx;
        float h = metrics.height * 1.0/64.0 * sy;

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

        s -= 4 * sy;

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
        for (int i = 0; i < f->font.num_fonts; i++)
                f->font.fonts[i].num_glyphs_in_vbo = 0;
        f->font.num_decoration = 0;

        for (int i = 0; i < f->row; i++) {
                for (int j = 0; j < f->col; j++) {
                        if (!f->line[i][j].c) continue;
                        //printf("U+%x at %d,%d\n", f->line[i][j].c, j, i);
                        render_glyph(f, f->line[i][j], j, i);
                }
        }

        /* Add the cursor to the decoration VBO. */
        if (f->mode & MODE_CURSOR_VISIBLE)
                render_cursor(f);

        /* Render the quads. */
        glUniform1i(f->uniform_is_solid, 1);

        glBindBuffer(GL_ARRAY_BUFFER, f->font.vbo_decoration);
        glEnableVertexAttribArray(f->attribute_coord);

        /* Upload the VBO. */
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

        /* Upload the VBO. */
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

        f->font.num_fonts = 1;
        for (int i = 0; i < f->font.num_fonts; i++) {
                struct font *font = f->font.fonts + i;

                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, font->sprite_texture);
                glUniform1i(f->uniform_tex, 0);
                glUniform1i(f->uniform_is_solid, 0);

                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

                glBindBuffer(GL_ARRAY_BUFFER, font->vbo_vertices);
                glEnableVertexAttribArray(f->attribute_coord);

                /* Upload the VBO. */
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

                /* Upload the VBO. */
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

                if (font->spritemap_dirty) {
                        /* Upload the sprite map. */
                        glTexImage2D(GL_TEXTURE_2D,
                                0,
                                GL_ALPHA,
                                2048,
                                2048,
                                0,
                                GL_ALPHA,
                                GL_UNSIGNED_BYTE,
                                font->sprite_buffer);
                        font->spritemap_dirty = 0;
                }

                glDrawArrays(GL_TRIANGLES, 0, font->num_glyphs_in_vbo * 6);
        }
}

void display(struct frame *f)
{
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        render(f);
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
                //"NotoColorEmoji.ttf",
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
                        printf("%d,%d\n", f->w.cw, f->w.ch);
                }

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
                        .sprite_buffer = calloc(1, 2048 * 2048),
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

#define UTF8CONT(X) (((uint8_t)(X) & 0xc0) == 0x80)

unsigned utf8chrlen(const char *s, unsigned l)
{
	unsigned i = 0;
	while (s++ && ++i < l && UTF8CONT(*s));
	return i;
}

unsigned utf8decode(const char *s, unsigned l, uint32_t *c)
{
	unsigned len = utf8chrlen(s, l);

        *c = (*s & ((1 << (8 - len)) - 1))
                << (len - 1) * 6;
	for (unsigned i = 1; i < len; i++)
		*c |= (s[i] & 0x3F) << (len - i - 1) * 6;

	return len;
}

int utf8encode(uint32_t c, uint8_t *buf, unsigned *len)
{
	if (c <= 0x7F) {
		*len = 1;
		buf[0] = c & 0xFF;
	} else if (c >= 0x80 && c <= 0x7FF) {
		*len = 2;
		buf[0] = 0xC0 | ((c >> 6) & 0x1F);
		buf[1] = 0x80 | (c & 0x3F);
	} else if (c >= 0x800 && c <= 0xFFFF) {
		*len = 3;
		buf[0] = 0xE0 | ((c >> 12) & 0xF);
		buf[1] = 0x80 | ((c >> 6) & 0x3F);
		buf[2] = 0x80 | (c & 0x3F);
	} else if (c >= 0x10000 && c <= 0x10FFFF) {
		*len = 4;
		buf[0] = 0xF0 | ((c >> 18) & 0x7);
		buf[1] = 0x80 | ((c >> 12) & 0x3F);
		buf[2] = 0x80 | ((c >> 6) & 0x3F);
		buf[3] = 0x80 | (c & 0x3F);
	} else return 1;

	return 0;
}

void tresize(struct frame *f, int col, int row)
{
        dprintf("tresize(%d,%d -> %d,%d)\n", f->col, f->row, col, row);

        f->line = realloc(f->line, row * sizeof *f->line);

        for (int i = f->row; i < row; i++) {
                f->line[i] = calloc(f->col, sizeof *f->line[i]);
        }

        for (int i = 0; i < row; i++) {
                f->line[i] = realloc(f->line[i], col * sizeof *f->line[i]);
                if (col > f->col)
                        memset(&f->line[i][f->col], 0, (col - f->col) * sizeof **f->line);
        }

        f->col = col;
        f->row = row;
}

void tprintc(struct frame *f, uint32_t c)
{
        dprintf("tputc(U+%x/%c) (%d,%d)\n", c, c, f->c.x, f->c.y);
        f->line[f->c.y][f->c.x] = (struct glyph){
                .c = c,
                .mode = f->c.mode,
        };
}

void tclearregion(struct frame *f, int x0, int y0, int x1, int y1)
{
        dprintf("tclearregion(%d,%d,%d,%d)\n", x0, y0, x1, y1);

        for (int i = y0; i <= y1; i++)
                for (int j = x0; j <= x1; j++)
                        f->line[i][j] = (struct glyph){ 0, 0 };
}

int limit(int *x, int y, int z)
{
        if (*x < y) *x = y;
        if (*x > z) *x = z;
        return *x;
}

void tmoveto(struct frame *f, int x, int y)
{
        dprintf("tmoveto(%d,%d)\n", x, y);

	int miny, maxy;

	if (f->c.state & CURSOR_ORIGIN) {
		miny = f->top;
		maxy = f->bot;
	} else {
		miny = 0;
		maxy = f->row - 1;
	}

	f->c.state &= ~CURSOR_WRAPNEXT;
	f->c.x = limit(&x, 0, f->col - 1);
	f->c.y = limit(&y, miny, maxy);
}

void tmoveato(struct frame *f, int x, int y)
{
        dprintf("tmoveato(%d,%d)\n", x, y);
	tmoveto(f, x, y + ((f->c.state & CURSOR_ORIGIN) ? f->top: 0));
}

void tsetscroll(struct frame *f, int top, int bot)
{
        limit(&bot, 0, f->row - 1);
        limit(&top, 0, f->row - 1);

        if (top > bot) {
                int tmp = bot;
                bot = top;
                top = tmp;
        }

        f->top = top;
        f->bot = bot;
}

void tscrolldown(struct frame *f, int orig, int n)
{
        tclearregion(f, 0, f->bot - n + 1, f->col - 1, f->bot);

        for (int i = f->bot; i >= orig + n; i--) {
                struct glyph *temp = f->line[i];
                f->line[i] = f->line[i - n];
                f->line[i - n] = temp;
        }
}

void tscrollup(struct frame *f, int orig, int n)
{
        dprintf("tscrollup(%d,%d)\n", orig, n);

        tclearregion(f, 0, orig, f->col - 1, orig + n - 1);

        for (int i = orig; i <= f->bot - n; i++) {
                struct glyph *tmp = f->line[i];
                f->line[i] = f->line[i + n];
                f->line[i + n] = tmp;
        }
}

void tnewline(struct frame *f, int first_col)
{
        dprintf("tnewline(%d)\n", first_col);

        int y = f->c.y;

        if (y == f->bot) {
                tscrollup(f, f->top, 1);
        } else {
                y++;
        }

        tmoveto(f, first_col ? 0 : f->c.x, y);
}

void tcontrolcode(struct frame *f, uint32_t c)
{
        dprintf("control code %"PRIX32"\n", c);
        switch (c) {
        case ESC:
                f->esc |= ESC_START;
                break;
        case LF:
        case VT:
                tnewline(f, f->mode & MODE_CRLF);
                break;
        case CR:
                tmoveto(f, 0, f->c.y);
                break;
        case HT:
                tmoveto(f, (f->c.x / 8 + 1) * 8, f->c.y);
                break;
        case BEL:
                break;
        case BS:
                tmoveto(f, f->c.x - 1, f->c.y);
                tprintc(f, ' ');
                break;
        }
}

void csiparse(struct frame *f)
{
	char *p = f->csi.buf, *np;
	long v;

	f->csi.narg = 0;
	if (*p == '?') {
		f->csi.priv = 1;
		p++;
	}

	f->csi.buf[f->csi.len] = 0;
	while (p < f->csi.buf + f->csi.len) {
		np = NULL;
		v = strtol(p, &np, 10);
		if (np == p)
			break;
		if (v == LONG_MAX || v == LONG_MIN)
			v = -1;
		f->csi.arg[f->csi.narg++] = v;
		p = np;
		if ((*p != ';' && *p != ':') || f->csi.narg == ESC_ARG_SIZE)
			break;
		p++;
	}

	f->csi.mode[0] = *p++;
	f->csi.mode[1] = (p < f->csi.buf + f->csi.len) ? *p : 0;

        printf("(%.*s) -> ESC '[' [[ [<priv:%d>] ", f->csi.len, f->csi.buf, f->csi.priv);
        for (int i = 0; i < f->csi.narg; i++)
                printf("<arg:%ld> [;]", f->csi.arg[i]);
        printf("] <mode:%d:%c> [<mode:%d>]]", f->csi.mode[0], f->csi.mode[0], f->csi.mode[1]);
        puts("");
}

void get_csi_graphic_mode(long arg, int *mode)
{
        switch (arg) {
        case 0:
                *mode = 0;
                break;
        case 1: /* Bold */
                *mode |= GLYPH_BOLD;
                break;
        case 22: /* Turn off bold */
                *mode &= ~GLYPH_BOLD;
                break;
        case 4: /* Underline */
                *mode |= GLYPH_UNDERLINE;
                break;
        case 24: /* Turn off underline */
                *mode &= ~GLYPH_UNDERLINE;
                break;
        case 7:
                *mode |= GLYPH_INVERSE;
                break;
        case 27:
                *mode &= ~GLYPH_INVERSE;
                break;
        default:
                fprintf(stderr, "Unknown CSI sequence argument %ld\n", arg);
                break;
        }
}

void handle_terminal_mode(struct frame *f, int set)
{
        int mode = 0;

        switch (f->csi.arg[0]) {
        case 25: /* Make cursor visible */
                mode |= MODE_CURSOR_VISIBLE;
                break;
        }

        if (set) f->mode |= mode;
        else f->mode &= ~mode;
}

#define DEFAULT(x, y) (f->csi.narg ? (x) : (y))

void csihandle(struct frame *f)
{
        switch (f->csi.mode[0]) {
        case 'A': /* Move cursor up n lines */
                tmoveto(f, f->c.x, f->c.y - DEFAULT(f->csi.arg[0], 1));
                break;
        case 'B': /* Move cursor down n lines */
                tmoveto(f, f->c.x, f->c.y + DEFAULT(f->csi.arg[0], 1));
                break;
        case 'C': /* Move cursor right n lines */
                tmoveto(f, f->c.x + DEFAULT(f->csi.arg[0], 1), f->c.y);
                break;
        case 'D': /* Move cursor left n lines */
                tmoveto(f, f->c.x - DEFAULT(f->csi.arg[0], 1), f->c.y);
                break;
        case 'h': /* Set terminal mode */
                handle_terminal_mode(f, 1);
                break;
        case 'H': /* CUP - Move cursor too coordinates */
                if (!f->csi.narg)
                        tmoveto(f, 0, 0);
                else
                        tmoveto(f, f->csi.arg[1] - 1, f->csi.arg[0] - 1);
                break;
        case 'J': /* Clear screen */
                switch (f->csi.arg[0]) {
                case 0: /* below */
                        tclearregion(f, f->c.x, f->c.y, f->col - 1, f->c.y);
                        if (f->c.y < f->row - 1) {
                                tclearregion(f, 0, f->c.y + 1, f->col - 1, f->row - 1);
                        }
                        break;
                case 1: /* above */
                        if (f->c.y > 1)
                                tclearregion(f, 0, 0, f->col - 1, f->c.y - 1);
                        tclearregion(f, 0, f->c.y, f->c.x, f->c.y);
                        break;
                case 2: /* all */
                        tclearregion(f, 0, 0, f->col - 1, f->row - 1);
                        break;
                default:
                        fprintf(stderr, "Unknown clear argument %ld\n", f->csi.arg[0]);
                        break;
                }
                break;
        case 'K': /* EL - Clear line */
                if (!f->csi.narg || f->csi.arg[0] == 0)
                        tclearregion(f, f->c.x, f->c.y, f->col - 1, f->c.y);
                else if (f->csi.arg[0] == 1)
                        tclearregion(f, 0, f->c.y, f->c.x, f->c.y);
                else if (f->csi.arg[0] == 2)
                        tclearregion(f, 0, f->c.y, f->col - 1, f->c.y);
                break;
        case 'l': /* Reset terminal mode */
                handle_terminal_mode(f, 0);
                break;
        case 'L':
                tscrolldown(f, f->c.y, f->csi.narg ? f->csi.arg[0] : 1);
                break;
        case 'm':
                if (!f->csi.narg) {
                        get_csi_graphic_mode(0, &f->c.mode);
                } else {
                        for (int i = 0; i < f->csi.narg; i++)
                                get_csi_graphic_mode(f->csi.arg[i], &f->c.mode);
                }
                break;
        case 'M': /* DL - Delete n lines */
                tscrollup(f, f->c.y, f->csi.narg ? f->csi.arg[0] : 1);
                break;
        case 'r': /* DECSTBM - Set scroll region */
                if (!f->csi.narg) tsetscroll(f, 0, f->row - 1);
                else tsetscroll(f, f->csi.arg[0] - 1, f->csi.arg[1] - 1);
                tmoveato(f, 0, 0);
                break;
        case 'X': /* ECH - Erase n chars */
                tclearregion(f, f->c.x, f->c.y,
                        f->c.x + (f->csi.narg ? f->csi.arg[0] : 1), f->c.y);
                break;
        case ' ':
                if (f->csi.mode[1] == 'q') {
                        /* TODO: Set cursor style. */
                }
                break;
        default:
                fprintf(stderr, "Unhandled escape sequence\n");
                break;
        }
}

void resetcsi(struct frame *f)
{
        memset(&f->csi, 0, sizeof f->csi);
}

void resetesc(struct frame *f)
{
        f->esc = 0;
}

void eschandle(struct frame *f, uint32_t c)
{
        switch (c) {
        case '[':
                f->esc |= ESC_CSI;
                break;
        }
}

void tputc(struct frame *f, uint32_t c)
{
        //dprintf("tputc(U+%x/%c) (%d,%d)\n", c, c, f->c.x, f->c.y);

        /* TODO: ??? */
        if (f->c.x >= f->col) f->c.x = f->col - 1;

        /* Here's the legwork of actually interpreting commands. */

        if (ISCONTROL(c)) {
                tcontrolcode(f, c);
        } else if (f->esc & ESC_START) {
                if (f->esc & ESC_CSI) {
                        f->csi.buf[f->csi.len++] = c;
                        if ((c > 0x40 && c < 0x7E)
                                || f->csi.len >= sizeof(f->csi.buf) - 1) {
                                f->esc = 0;

                                /*
                                 * So now we have an entire escape sequence in
                                 * `f->esc_buf`, just parse it and execute it.
                                 */
                                csiparse(f);
                                csihandle(f);
                                resetcsi(f);
                                resetesc(f);
                        }
                        return;
                } else {
                        eschandle(f, c);
                }
        } else if (f->c.x < f->col) {
                tprintc(f, c);
                f->c.x++;
        }
}

int twrite(struct frame *f, const char *buf, int buflen)
{
        int charsize, n;

        for (n = 0; n < buflen; n += charsize) {
                /* TODO: Support commands which alter support for UTF-8. */
                uint32_t c;
                if (!(charsize = utf8decode(buf + n, buflen - n, &c))) break;

#if 0
                if (ISCONTROL(c) && 0) {
                        if (c & 0x80) {
                                c &= 0x7f;
                                tputc(f, '^');
                                tputc(f, '[');
                        } else if (c != '\n' && c != '\r' && c != '\t') {
                                c ^= 0x40;
                                tputc(f, '^');
                        }
                }
#endif

                tputc(f, c);
        }

        return n;
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

        display(k);
        glfwSwapBuffers(window);

        while (!glfwWindowShouldClose(window) && !k->shell_done) {
                display(k);
                glfwSwapBuffers(window);
                glfwPollEvents();
        }

        free_resources(k);
        glfwTerminate();

        return 0;
}
