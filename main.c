#include <stdlib.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <GL/glew.h>
#include <GL/gl.h>
#include <GLFW/glfw3.h>

#define WINDOW_WIDTH 600
#define WINDOW_HEIGHT 300

/* Global kty state. */
struct kty {
        GLuint program;

        GLint attribute_coord;
        GLint uniform_tex;
        GLint uniform_color;

        GLuint vbo;

        FT_Library ft;
        FT_Face face;
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

int bind_uniform_to_program(struct kty *k, const char *name)
{
        int coord = glGetUniformLocation(k->program, name);

        if (coord < 0) {
                fprintf(stderr, "Couldn't bind uniform %s\n", name);
                exit(1);
        }

        return coord;
}

int bind_attribute_to_program(struct kty *k, const char *name)
{
        int coord = glGetAttribLocation(k->program, name);

        if (coord < 0) {
                fprintf(stderr, "Couldn't bind attribute %s\n", name);
                exit(1);
        }

        return coord;
}

int init_gl_resources(struct kty *k)
{
        const char vshader[] = "#version 120\n\
attribute vec4 coord;\n\
varying vec2 texpos;\n\
void main(void) {\n\
        gl_Position = vec4(coord.xy, 0, 1);\n\
        texpos = coord.zw;\n\
}";

        const char fshader[] = "#version 120\n\
varying vec2 texpos;\n\
uniform sampler2D tex;\n\
uniform vec4 color;\n\
void main(void) {\n\
        gl_FragColor = vec4(1, 1, 1, texture2D(tex, texpos).a) * color;\n\
}";

        /* Compile each shader. */
        GLuint vs = create_shader(vshader, GL_VERTEX_SHADER);
        GLuint fs = create_shader(fshader, GL_FRAGMENT_SHADER);

        if (!vs || !fs) return 1;

        /* Create the program and link the shaders. */
        k->program = glCreateProgram();
        glAttachShader(k->program, vs);
        glAttachShader(k->program, fs);
        glLinkProgram(k->program);

        /* Now check that everything compiled and linked okay. */
        GLint link_ok = GL_FALSE;
        glGetProgramiv(k->program, GL_LINK_STATUS, &link_ok);

        if (!link_ok) {
                fprintf(stderr, "glLinkProgram:");
                print_gl_error_log(k->program);
                return 1;
        }

        k->attribute_coord = bind_attribute_to_program(k, "coord");
        k->uniform_tex = bind_uniform_to_program(k, "tex");
        k->uniform_color = bind_uniform_to_program(k, "color");

        /* Get a free VBO number. */
        glGenBuffers(1, &k->vbo);

        return 0;
}

/**
 * Render text using the currently loaded font and currently set font size.
 * Rendering starts at coordinates (x, y), z is always 0.
 * The pixel coordinates that the FreeType2 library uses are scaled by (sx, sy).
 */
void render_text(struct kty *k, const char *text, float x, float y, float sx, float sy)
{
        FT_GlyphSlot g = k->face->glyph;

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

        for (const char *c = text; *c; c++) {
                if (FT_Load_Char(k->face, *c, FT_LOAD_RENDER))
                        continue;

                glTexImage2D(GL_TEXTURE_2D,
                        0,
                        GL_ALPHA,
                        g->bitmap.width,
                        g->bitmap.rows,
                        0,
                        GL_ALPHA,
                        GL_UNSIGNED_BYTE,
                        g->bitmap.buffer);

                /* Calculate the vertex and texture coordinates */
                float x2 = x + g->bitmap_left * sx;
                float y2 = -y - g->bitmap_top * sy;
                float w = g->bitmap.width * sx;
                float h = g->bitmap.rows * sy;

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

                x += (g->advance.x >> 6) * sx;
                y += (g->advance.y >> 6) * sy;
        }

        glDisableVertexAttribArray(k->attribute_coord);
        glDeleteTextures(1, &tex);
}

void display(struct kty *k)
{
        float sx = 2.0 / WINDOW_WIDTH;
        float sy = 2.0 / WINDOW_HEIGHT;

        glUseProgram(k->program);
        glClearColor(1, 1, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        /* Enabling blending allows us to use alpha textures. */
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUniform4fv(k->uniform_color, 1, (GLfloat []){ 0, 0, 0, 1 });

        FT_Set_Pixel_Sizes(k->face, 0, 12);

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

        render_text(k, buf, -1 + 8 * sx, 1 - (16 * 1) * sy, sx, sy);
}

void free_resources(struct kty *k)
{
        glDeleteProgram(k->program);
}

int init_freetype(struct kty *k)
{
        if (FT_Init_FreeType(&k->ft)) {
                fprintf(stderr, "Could not init freetype library\n");
                return 1;
        }

        if (FT_New_Face(k->ft, "SourceCodePro-Regular.otf", 0, &k->face)) {
                fprintf(stderr, "Could not open font\n");
                return 1;
        }

        return 0;
}

int main(void)
{
        struct kty *k = malloc(sizeof *k);
        if (!k) return 1;

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
        if (init_freetype(k)) return 1;

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
