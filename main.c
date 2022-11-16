#include <stdlib.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <freetype/tttables.h>
#include <GL/glew.h>
#include <GL/gl.h>
#include <GLFW/glfw3.h>

#include "kdg/include/kdgu.h"

/* No one will ever need more than 16 fonts. */
#define MAX_FONTS 16
#define WINDOW_WIDTH 600
#define WINDOW_HEIGHT 300
#define FONT_SIZE 18

struct font {
        const char *path;         /* The path that this font was loaded from. */
        int is_color_font;

        FT_Face face;
        FT_Render_Mode render_mode;

        int pixel_size;
        int load_flags;
};

/* Global kty state. */
struct kty {
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

        /* Fonts */
        struct font fonts[MAX_FONTS];
        int num_fonts;
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

int render_glyph(struct kty *k, uint32_t c, float *x, float *y, float sx, float sy)
{
        struct font *f = NULL;
        int load_flags = FT_LOAD_COLOR;

        for (int i = 0; i < k->num_fonts; i++) {
                f = k->fonts + i;
                FT_Face face = f->face;
                FT_Set_Pixel_Sizes(face, 0, FONT_SIZE);

                uint32_t glyph_index = FT_Get_Char_Index(face, c);
                if (!glyph_index) continue;

                if (f->is_color_font) {
                        if (!face->num_fixed_sizes) return 1;

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

        if (!f) {
                fprintf(stderr, "No glyph found for U+%x\n", c);
                return 1;
        }

        FT_GlyphSlot slot = f->face->glyph;

        if (slot->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA) {
                glUniform1i(k->uniform_is_color, 1);
                glTexImage2D(GL_TEXTURE_2D,                         /* target */
                        0,                                     /* GLint level */
                        4,                      /* GLint internalformat */
                        slot->bitmap.width,                  /* GLsizei width */
                        slot->bitmap.rows,                  /* GLsizei height */
                        0,                                    /* GLint border */
                        GL_BGRA,                             /* GLenum format */
                        GL_UNSIGNED_BYTE,                      /* GLenum type */
                        slot->bitmap.buffer);             /* const void * dat */
        } else {
                glUniform1i(k->uniform_is_color, 0);
                glTexImage2D(GL_TEXTURE_2D,                         /* target */
                        0,                                     /* GLint level */
                        GL_ALPHA,                     /* GLint internalformat */
                        slot->bitmap.width,                  /* GLsizei width */
                        slot->bitmap.rows,                  /* GLsizei height */
                        0,                                    /* GLint border */
                        GL_ALPHA,                            /* GLenum format */
                        GL_UNSIGNED_BYTE,                      /* GLenum type */
                        slot->bitmap.buffer);             /* const void * dat */
        }

        glGenerateMipmap(GL_TEXTURE_2D);

        FT_Glyph_Metrics metrics = slot->metrics;

        float tmp = metrics.width * 1.0/64.0;
        if (tmp > FONT_SIZE * 2) {
                float s = (FONT_SIZE * 2.0) / tmp;
                sx *= s;
                sy *= s;
        }

        /* Calculate the vertex and texture coordinates */
        float x2 = *x + metrics.horiBearingX * 1.0/64.0 * sx;
        float y2 = -*y - slot->bitmap_top * sy;
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

        *x += metrics.horiAdvance * 1.0/64.0 * sx;
        //*y += metrics.vertAdvance * 1.0/64.0 * sy;

        return 0;
}

int is_color_font(FT_Face face)
{
        static const uint32_t tag = FT_MAKE_TAG('C', 'B', 'D', 'T');
        unsigned long length = 0;
        FT_Load_Sfnt_Table(face, tag, 0, NULL, &length);
        return !!length;
}

void render_text(struct kty *k, const char *text, float x, float y, float sx, float sy)
{
        GLuint tex;
        glActiveTexture(GL_TEXTURE0);
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glUniform1i(k->uniform_tex, 0);

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glEnableVertexAttribArray(k->attribute_coord);
        glBindBuffer(GL_ARRAY_BUFFER, k->vbo);
        glVertexAttribPointer(k->attribute_coord, 4, GL_FLOAT, GL_FALSE, 0, 0);

        kdgu *s = kdgu_news(text);
        unsigned idx = 0;
        uint32_t c = kdgu_decode(s, idx);
        render_glyph(k, c, &x, &y, sx, sy);

        while (kdgu_next(s, &idx)) {
                c = kdgu_decode(s, idx);
                if (c == (uint32_t)-1) continue;
                if (render_glyph(k, c, &x, &y, sx, sy))
                        fprintf(stderr, "Couldn't render code point U+%x\n", c);
        }

        glDisableVertexAttribArray(k->attribute_coord);
        glDeleteTextures(1, &tex);
}

void display(struct kty *k)
{
        float sx = 2.0 / WINDOW_WIDTH;
        float sy = 2.0 / WINDOW_HEIGHT;

        glClearColor(0.1, 0.1, 0.1, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(k->program);

        /* Enabling blending allows us to use alpha textures. */
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUniform4fv(k->uniform_color, 1, (GLfloat []){ 0, 0.5, 1, 1 });

        /* Set up a quick FPS counter. */
        static double previous_time = 0, current_time = 0;
        static int frame_count = 0;
        static char buf[256];

        current_time = glfwGetTime();
        frame_count++;

        if (current_time - previous_time >= 1.0) {
                printf("%f ms/frame\n", 1000.0 / (double)frame_count);
                snprintf(buf, sizeof buf, "%f ms/frame (%f fps)",
                        1000.0 / (double)frame_count,
                        (double)frame_count);
                frame_count = 0;
                previous_time += 1.0;
        }

        static char history[10000];
        static int history_len = 0;
        char c[] = { 0, 0 };
        read(k->master, c, 1);
        history[history_len++] = c[0];
        history[history_len] = 0;
        render_text(k, history, -1 + 8 * sx, 1 - (16 * 1) * sy, sx, sy);
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

        int num_path = 3;

        for (int i = 0; i < num_path; i++) {
                FT_Face face;

                if (FT_New_Face(k->ft, path[i], 0, &face)) {
                        fprintf(stderr, "Could not open font ‘%s’\n", path[i]);
                        return 1;
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
        struct kty *k = malloc(sizeof *k);
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

        /* Create the VBO, shader program, etc. */
        if (init_gl_resources(k)) return 1;

        while (!glfwWindowShouldClose(window)) {
                display(k);
                glfwSwapBuffers(window);
                glfwPollEvents();
        }

        free_resources(k);
        glfwTerminate();

        return 0;
}
