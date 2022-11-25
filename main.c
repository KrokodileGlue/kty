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
#include "render.h"

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

int main(int, char **argv, char **env)
{
        k = frame_new(env);

        if (!glfwInit()) return 1;

        GLFWwindow *window = glfwCreateWindow(WINDOW_WIDTH,
                                              WINDOW_HEIGHT,
                                              argv[0],
                                              NULL,
                                              NULL);

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
