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
#include "font.h"

void free_resources(struct frame *f)
{
        glDeleteProgram(f->program);
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
