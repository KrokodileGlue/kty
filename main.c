#define _XOPEN_SOURCE 600

#include <stdlib.h>
#include <stdbool.h>
#include <time.h>

#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <signal.h>

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
#define NUM_GLYPH 1000

enum {
        ESC = 0x1B,
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
        GLuint vbo;
        int num_glyphs_in_vbo;

        int num_glyph;              /* The number of glyphs in the spritemap. */

        int pixel_size;
        int load_flags;
};

struct glyph {
        uint32_t c;
        enum {
                GLYPH_NONE = 0,
                GLYPH_WRAP = 1 << 0,
        } mode;
};

struct cursor {
        int x, y;
};

struct window {
        int width, height;
        int cw, ch;
};

struct font_renderer {
        /* FreeType */
        FT_Library ft;

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
};

struct frame {
        /* This will be a pointer to a global font manager eventually. */
        struct font_renderer font;

        /* PTY */
        int master;

        /* OpenGL */
        GLuint program;

        GLint attribute_coord;
        GLint uniform_tex;
        GLint uniform_color;
        GLint uniform_is_color;

        GLuint vbo;

        /* State */
        struct window w;
        int col, row;
        struct cursor c;
        struct glyph **line;

        bool shell_done; /* bazinga */
};

#define UTF8CONT(X) (((uint8_t)(X) & 0xc0) == 0x80)

unsigned
utf8chrlen(const uint8_t *s, unsigned l)
{
	unsigned i = 0;
	while (s++ && ++i < l && UTF8CONT(*s));
	return i;
}

uint32_t utf8decode(const uint8_t *s, unsigned l)
{
	uint32_t c;
	unsigned len = utf8chrlen(s, l);

	c = (*s & ((1 << (8 - len)) - 1))
		<< (len - 1) * 6;
	for (unsigned i = 1; i < len; i++)
		c |= (s[i] & 0x3F) << (len - i - 1) * 6;

	return c;
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
        printf("tresize(%d,%d -> %d,%d)\n", f->col, f->row, col, row);

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

void tputc(struct frame *f, uint32_t c)
{
        printf("tputc(U+%x) (%d,%d)\n", c, f->c.x, f->c.y);

        if (f->c.x >= f->col) f->c.x = f->col - 1;
        if (f->c.y >= f->row) {
                int diff = f->c.y - f->row + 1;
                for (int i = diff; i < f->row; i++) {
                        f->line[i - diff] = f->line[i];
                }
                for (int i = f->row - diff; i < f->row; i++)
                        f->line[i] = calloc(f->col, sizeof *f->line[i]);
                f->c.y -= diff;
        }

        f->line[f->c.y][f->c.x] = (struct glyph){
                .c = c,
                .mode = GLYPH_NONE,
        };
}

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
attribute vec4 coord;\n\
varying vec2 texpos;\n\
void main(void) {\n\
        gl_Position = vec4(coord.xy, 0, 1);\n\
        texpos = coord.zw;\n\
}";

        const char fs[] = "#version 120\n\
varying vec2 texpos;\n\
uniform sampler2D tex;\n\
uniform vec4 color;\n\
uniform bool is_color;\n\
void main(void) {\n\
        //gl_FragColor = texture2D(tex, texpos).a * color + vec4(0.25, 0.25, 0, 1);\n\
        gl_FragColor = vec4(1, 1, 1, texture2D(tex, texpos).a) * color + vec4(0.25, 0.25, 0.25, 0.1);\n\
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
        f->uniform_tex = bind_uniform_to_program(f->program, "tex");
        f->uniform_color = bind_uniform_to_program(f->program, "color");

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

        printf("Adding sprite U+%x (%c) to font %s\n", c, c, font->path);

        FT_GlyphSlot slot = font->face->glyph;

        int sh = 2048 / f->w.ch;
        int sw = 2048 / f->w.cw;
        int x = (font->num_glyphs_in_vbo % sw) * f->w.cw;
        int y = (font->num_glyphs_in_vbo / sw) * f->w.ch;

        printf("%d,%d,%d,%d\n", slot->bitmap.rows, slot->bitmap.width, f->w.ch, f->w.cw);

        /* Write the sprite into the spritemap. */
        for (unsigned i = 0; i < slot->bitmap.rows; i++) {
                if (!slot->bitmap.buffer) break;
                memcpy(font->sprite_buffer + (y + i) * sw + x,
                        slot->bitmap.buffer + (i * slot->bitmap.width),
                        slot->bitmap.width);
        }

        float fx0 = (float)x / 2048.0;
        float fx1 = (float)(x + slot->bitmap.width) / 2048.0;
        float fy0 = 1.0 - (float)y / 2048.0;
        float fy1 = 1.0 - (float)(y + slot->bitmap.rows) / 2048.0;

        f->font.glyph[f->font.num_glyph] = (struct sprite){
                .c = c,
                .metrics = slot->metrics,
                .bitmap_top = slot->bitmap_top,
                .tex_coords = { fx0, fx1, fy0, fy1 },
                .font = font,
        };

        printf("[%f, %f]\n", fx0, fy0);
        printf("[%f, %f]\n", fx1, fy0);
        printf("[%f, %f]\n", fx0, fy1);
        printf("[%f, %f]\n", fx1, fy1);

        font->spritemap_dirty = 1;

        return &f->font.glyph[f->font.num_glyph++];
}

int render_glyph(struct frame *f, uint32_t c, int x0, int y0)
{
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
        float y = 1 - (f->w.ch * (1 + y0)) * sy;
        float x2 = x + metrics.horiBearingX * 1.0/64.0 * sx;
        float y2 = -y - sprite->bitmap_top * sy;
        float w = metrics.width * 1.0/64.0 * sx;
        float h = metrics.height * 1.0/64.0 * sy;

        struct point {
                GLfloat x;
                GLfloat y;
                GLfloat s;
                GLfloat t;
        } box[6] = {
                { x2    , -y2    , sprite->tex_coords[0], sprite->tex_coords[0] },
                { x2 + w, -y2    , sprite->tex_coords[1], sprite->tex_coords[0] },
                { x2    , -y2 - h, sprite->tex_coords[0], sprite->tex_coords[1] },

                { x2 + w, -y2 - h, sprite->tex_coords[1], sprite->tex_coords[1] },
                { x2 + w, -y2    , sprite->tex_coords[1], sprite->tex_coords[0] },
                { x2    , -y2 - h, sprite->tex_coords[0], sprite->tex_coords[1] },
        };

//        puts("===");
//        for (int i = 0; i < 6; i++) {
//                printf("%f, %f\n", box[i].x, box[i].y);
//        }

        struct font *font = sprite->font;

        memcpy(font->vertices + font->num_glyphs_in_vbo * sizeof box, box, sizeof box);
        font->num_glyphs_in_vbo++;

        return 0;
}

void render(struct frame *f)
{
        for (int i = 0; i < f->font.num_fonts; i++)
                f->font.fonts[i].num_glyphs_in_vbo = 0;

        for (int i = 0; i < f->row; i++) {
                for (int j = 0; j < f->col; j++) {
                        if (!f->line[i][j].c) continue;
                        render_glyph(f, f->line[i][j].c, j, i);
                }
        }

        /*
         * So each glyph has been rendered into its font's spritesheet at this
         * point.
         */

        f->font.num_fonts = 1;
        for (int i = 0; i < f->font.num_fonts; i++) {
                struct font *font = f->font.fonts + i;
                //printf("Rendering %s\n", font->path);

                /* Enabling blending allows us to use alpha textures. */
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glUniform4fv(f->uniform_color, 1, (GLfloat []){ 1, 0, 1, 1 });

                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, font->sprite_texture);
                glUniform1i(f->uniform_tex, 0);

                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

                glBindBuffer(GL_ARRAY_BUFFER, font->vbo);
                glEnableVertexAttribArray(f->attribute_coord);

                /* Upload the VBO. */
                glBufferData(GL_ARRAY_BUFFER,
                        font->num_glyphs_in_vbo * 6 * 4 * sizeof(GLfloat),
                        font->vertices,
                        GL_DYNAMIC_DRAW);

                glVertexAttribPointer(f->attribute_coord,
                        4,
                        GL_FLOAT,
                        GL_FALSE,
                        4 * sizeof(GLfloat),
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
        glClearColor(0.1, 0.1, 0.1, 1);
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
                        .vertices = calloc(2000, 4 * sizeof(GLfloat)),
                        .sprite_buffer = calloc(1, 2048 * 2048),
                };

                /* Get a free VBO number. */
                glGenBuffers(1, &f->font.fonts[f->font.num_fonts - 1].vbo);
                glGenTextures(1, &f->font.fonts[f->font.num_fonts - 1].sprite_texture);
        }

        return 0;
}

struct frame *k;

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

        switch (key) {
        case GLFW_KEY_ENTER:
                write(k->master, (char []){'\n'}, 1);
                break;
        case GLFW_KEY_BACKSPACE:
                write(k->master, (char []){8}, 1);
                break;
        }
}

void window_size_callback(GLFWwindow *window, int width, int height)
{
        (void)window;
        k->w.width = width, k->w.height = height;
        tresize(k, width / k->w.cw, height / k->w.ch);
        glViewport(0, 0, width, height);
}

void *read_shell(void *arg)
{
        (void)arg;

        while (1) {
                char c;
                if (read(k->master, &c, 1) < 0) break;

                switch (c) {
                        case ESC:
                                read(k->master, &c, 1);

                                switch (c) {
                                        case '[':
                                                read(k->master, &c, 1);

                                                switch (c) {
                                                        case 'K':
                                                                for (int i = k->c.x; i < k->row; i++)
                                                                        k->line[k->c.y][i].c = 0;
                                                                break;
                                                }
                                                break;
                                }
                                break;
                        case '\n':
                                k->c.x = 0;
                                k->c.y++;
                                break;
                        case 7:
                                /* bell */
                                break;
                        case 8:
                                k->c.x--;
                                break;
                        default: {
                                         static uint8_t buf[4];
                                         static int n = 0;

                                         if (UTF8CONT(c)) {
                                                 buf[n++ + 1] = c;
                                                 k->c.x--;
                                                 tputc(k, utf8decode(buf, n + 1));
                                                 k->c.x++;
                                         } else {
                                                 if (n) {
                                                         n = 0;
                                                 }

                                                 tputc(k, c & 0xff);
                                                 k->c.x++;
                                                 buf[0] = c & 0xff;
                                         }
                                 }
                }
        }

        k->shell_done = true;

        return NULL;
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

                execle("/bin/sh", "/bin/sh", NULL, env);
        } else {
                close(slave);
        }

        /* The shell is running, now set up the window/graphics. */
        k = calloc(sizeof *k, 1);
        if (!k) return 1;

        k->master = master;

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

        /* Create the VBO, shader program, etc. */
        if (init_gl_resources(k)) return 1;

        pthread_t shell_reader;
        pthread_create(&shell_reader, NULL, read_shell, &(int){0});

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
