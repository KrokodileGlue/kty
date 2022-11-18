#define _XOPEN_SOURCE 600

#include <stdlib.h>
#include <time.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/select.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <freetype/tttables.h>
#include <GL/glew.h>
#include <GL/gl.h>

#include <X11/Xlibint.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

/* No one will ever need more than 16 fonts. */
#define MAX_FONTS 16
#define WINDOW_WIDTH 600
#define WINDOW_HEIGHT 700
#define FONT_SIZE 12
#define MAX_LATENCY 33
#define MIN_LATENCY 8
#define NUM_GLYPH 1000
#define TIMEDIFF(A, B) \
        ((A.tv_sec - B.tv_sec) * 1000 + (A.tv_nsec - B.tv_nsec) / 1E6)

enum {
        ESC = 0x1B,
};

struct font {
        const char *path;         /* The path that this font was loaded from. */
        int is_color_font;

        FT_Face face;
        FT_Render_Mode render_mode;

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

/* Global kty state. */
struct kty {
        Display *x11_display;

        /* PTY */
        int master;

        /* OpenGL */
        GLuint program;

        GLint attribute_coord;
        GLint uniform_tex;
        GLint uniform_color;
        GLint uniform_is_color;

        GLuint vbo;

        /* FreeType */
        FT_Library ft;

        struct glyph_bitmap {
                uint32_t c;
                char *bitmap;
                FT_Glyph_Metrics metrics;
                int bitmap_top;
                int width, height;
        } glyph[NUM_GLYPH];
        int num_glyph;

        /* Fonts */
        struct font fonts[MAX_FONTS];
        int num_fonts;

        /* State */
        struct window w;
        int col, row;
        struct cursor c;
        struct glyph **line;
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

void tresize(struct kty *k, int col, int row)
{
        printf("tresize(%d,%d -> %d,%d)\n", k->col, k->row, col, row);

        k->line = realloc(k->line, row * sizeof *k->line);

        for (int i = k->row; i < row; i++) {
                k->line[i] = calloc(k->col, sizeof *k->line[i]);
        }

        for (int i = 0; i < row; i++) {
                k->line[i] = realloc(k->line[i], col * sizeof *k->line[i]);
                if (col > k->col)
                        memset(&k->line[i][k->col], 0, (col - k->col) * sizeof **k->line);
        }

        k->col = col;
        k->row = row;
}

void tputc(struct kty *k, uint32_t c)
{
        printf("tputc(U+%x) (%d,%d)\n", c, k->c.x, k->c.y);

        if (k->c.x >= k->col) k->c.x = k->col - 1;
        if (k->c.y >= k->row) {
                int diff = k->c.y - k->row + 1;
                for (int i = diff; i < k->row; i++) {
                        k->line[i - diff] = k->line[i];
                }
                for (int i = k->row - diff; i < k->row; i++)
                        k->line[i] = calloc(k->col, sizeof *k->line[i]);
                k->c.y -= diff;
        }

        k->line[k->c.y][k->c.x] = (struct glyph){
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

int init_gl_resources(struct kty *k)
{
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
        gl_FragColor = is_color\n\
                ? texture2D(tex, texpos).rgba\n\
                : vec4(1, 1, 1, texture2D(tex, texpos).a) * color;\n\
}";

        /* Compile each shader. */
        GLuint gvs = create_shader(vs, GL_VERTEX_SHADER);
        GLuint gfs = create_shader(fs, GL_FRAGMENT_SHADER);

        if (!gvs || !gfs) return 1;

        /* Create the program and link the shaders. */
        k->program = glCreateProgram();
        glAttachShader(k->program, gvs);
        glAttachShader(k->program, gfs);
        glLinkProgram(k->program);

        /* Now check that everything compiled and linked okay. */
        GLint link_ok = GL_FALSE;
        glGetProgramiv(k->program, GL_LINK_STATUS, &link_ok);

        if (!link_ok) {
                fprintf(stderr, "glLinkProgram:");
                print_gl_error_log(k->program);
                return 1;
        }

        k->attribute_coord = bind_attribute_to_program(k->program, "coord");
        k->uniform_tex = bind_uniform_to_program(k->program, "tex");
        k->uniform_color = bind_uniform_to_program(k->program, "color");
        k->uniform_is_color = bind_uniform_to_program(k->program, "is_color");

        /* Get a free VBO number. */
        glGenBuffers(1, &k->vbo);

        return 0;
}

struct glyph_bitmap *get_bitmap(struct kty *k, uint32_t c)
{
        for (int i = 0; i < k->num_glyph; i++)
                if (k->glyph[i].c == c)
                        return k->glyph + i;

        puts("here");

        struct font *f = NULL;
        int load_flags = FT_LOAD_COLOR;

        for (int i = 0; i < k->num_fonts; i++) {
                f = k->fonts + i;
                FT_Face face = f->face;
                FT_Set_Pixel_Sizes(face, 0, FONT_SIZE);

                uint32_t glyph_index = FT_Get_Char_Index(face, c);
                if (!glyph_index) continue;

                if (f->is_color_font) {
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
                        if (FT_Render_Glyph(face->glyph, f->render_mode)) continue;
                } else {
                        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) continue;
                }

                break;
        }

        if (!f) return NULL;

        FT_GlyphSlot slot = f->face->glyph;

        k->glyph[k->num_glyph] = (struct glyph_bitmap){
                .c = c,
                .bitmap = malloc(slot->bitmap.width * slot->bitmap.rows),
                .metrics = slot->metrics,
                .bitmap_top = slot->bitmap_top,
                .width = slot->bitmap.width,
                .height = slot->bitmap.rows,
        };

        memcpy(k->glyph[k->num_glyph].bitmap,
                f->face->glyph->bitmap.buffer,
                slot->bitmap.width * slot->bitmap.rows);

        return &k->glyph[k->num_glyph++];
}

int render_glyph(struct kty *k, uint32_t c, int x0, int y0)
{
        struct glyph_bitmap *slot = get_bitmap(k, c);

        if (!slot) {
                fprintf(stderr, "No glyph found for U+%x\n", c);
                return 1;
        }

        glTexImage2D(GL_TEXTURE_2D,                         /* target */
                0,                                     /* GLint level */
                GL_ALPHA,                     /* GLint internalformat */
                slot->width,                         /* GLsizei width */
                slot->height,                       /* GLsizei height */
                0,                                    /* GLint border */
                GL_ALPHA,                            /* GLenum format */
                GL_UNSIGNED_BYTE,                      /* GLenum type */
                slot->bitmap);                    /* const void * dat */

        FT_Glyph_Metrics metrics = slot->metrics;

        float sx = 2.0 / k->w.width;
        float sy = 2.0 / k->w.height;

        /* Calculate the vertex and texture coordinates */
        float x = -1 + (k->w.cw * x0) * sx;
        float y = 1 - (k->w.ch * (1 + y0)) * sy;
        float x2 = x + metrics.horiBearingX * 1.0/64.0 * sx;
        float y2 = -y - slot->bitmap_top * sy;
        float w = metrics.width * 1.0/64.0 * sx;
        float h = metrics.height * 1.0/64.0 * sy;

        struct point {
                GLfloat x;
                GLfloat y;
                GLfloat s;
                GLfloat t;
        } box[4] = {
                { x2    , -y2    , 0, 0 },
                { x2 + w, -y2    , 1, 0 },
                { x2    , -y2 - h, 0, 1 },
                { x2 + w, -y2 - h, 1, 1 },
        };

        glBufferData(GL_ARRAY_BUFFER, sizeof box, box, GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        return 0;
}

int is_color_font(FT_Face face)
{
        static const uint32_t tag = FT_MAKE_TAG('C', 'B', 'D', 'T');
        unsigned long length = 0;
        FT_Load_Sfnt_Table(face, tag, 0, NULL, &length);
        return !!length;
}

void display(struct kty *k)
{
        glClearColor(0.1, 0.1, 0.1, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        for (int i = 0; i < k->row; i++) {
                for (int j = 0; j < k->col; j++) {
                        if (!k->line[i][j].c) continue;
                        render_glyph(k, k->line[i][j].c, j, i);
                }
        }
}

void free_resources(struct kty *k)
{
        glDeleteProgram(k->program);
}

int load_fonts(struct kty *k)
{
        /* TODO: Get fonts from command line options. */

        const char *path[] = {
                "SourceCodePro-Regular.otf",
                "NotoColorEmoji.ttf",
                "NotoSansCJK-Regular.ttc",
        };

        for (unsigned i = 0; i < sizeof path / sizeof *path; i++) {
                FT_Face face;

                if (FT_New_Face(k->ft, path[i], 0, &face)) {
                        fprintf(stderr, "Could not open font ‘%s’\n", path[i]);
                        return 1;
                }

                if (!i) {
                        FT_Set_Pixel_Sizes(face, 0, FONT_SIZE);
                        FT_Load_Char(face, 'x', FT_LOAD_COMPUTE_METRICS);
                        FT_GlyphSlot slot = face->glyph;
                        k->w.cw = slot->metrics.horiAdvance / 64.0;
                        k->w.ch = slot->metrics.vertAdvance / 64.0;
                        printf("%d,%d\n", k->w.cw, k->w.ch);
                }

                k->fonts[k->num_fonts++] = (struct font){
                        .path = path[i],
                        .face = face,
                        .is_color_font = is_color_font(face),
                        .render_mode = FT_RENDER_MODE_NORMAL,
                        .load_flags = 0,
                };
        }

        return 0;
}

struct kty *k;

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

        //glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_FALSE);

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

        k->x11_display = glfwGetX11Display();

        /* Initialize FreeType. */
        if (FT_Init_FreeType(&k->ft)) {
                fprintf(stderr, "Could not init FreeType\n");
                return 1;
        }

        /* Load fonts. */
        if (load_fonts(k)) return 1;

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

        int width, height;
        glfwGetWindowSize(window, &width, &height);
        window_size_callback(window, width, height);

        /* Create the VBO, shader program, etc. */
        if (init_gl_resources(k)) return 1;

        glUseProgram(k->program);

        /* Enabling blending allows us to use alpha textures. */
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUniform4fv(k->uniform_color, 1, (GLfloat []){ 1, 0.5, 0, 1 });

        GLuint tex;
        glActiveTexture(GL_TEXTURE0);
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glUniform1i(k->uniform_tex, 0);

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glEnableVertexAttribArray(k->attribute_coord);
        glBindBuffer(GL_ARRAY_BUFFER, k->vbo);
        glVertexAttribPointer(k->attribute_coord, 4, GL_FLOAT, GL_FALSE, 0, 0);

        fd_set readable;
        int maxfd = k->master > k->x11_display->fd ? k->master : k->x11_display->fd;

        int drawing = 0;
        struct timespec now, trigger;
        double timeout = -1;

        display(k);
        glfwSwapBuffers(window);

        while (!glfwWindowShouldClose(window)) {
                FD_ZERO(&readable);
                FD_SET(k->master, &readable);
                FD_SET(k->x11_display->fd, &readable);

                if (select(maxfd + 1, &readable, NULL, NULL, NULL) == -1) {
                        perror("select");
                        return 1;
                }

                clock_gettime(CLOCK_MONOTONIC, &now);

                if (FD_ISSET(k->master, &readable))
                {
                        char c;
                        read(k->master, &c, 1);

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

                        if (!drawing) {
                                trigger = now;
                                drawing = 1;
                        }
                        timeout = (MAX_LATENCY - TIMEDIFF(now, trigger)) \
                                  / MAX_LATENCY * MIN_LATENCY;
                        if (timeout > 0)
                                continue;

                        printf("%f\n", timeout);
                }

                drawing = 0;
                display(k);
                glfwSwapBuffers(window);
                glfwPollEvents();
        }

        glDisableVertexAttribArray(k->attribute_coord);
        glDeleteTextures(1, &tex);

        free_resources(k);
        glfwTerminate();

        return 0;
}
