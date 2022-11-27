#define _XOPEN_SOURCE 600

#include <stdlib.h>
#include <stdio.h>
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

#include <ft2build.h>
#include FT_FREETYPE_H
#include <freetype/tttables.h>

#include "gl.h"
#include "util.h"
#include "frame.h"
#include "t.h"
#include "esc.h"
#include "utf8.h"
#include "font.h"
#include "render.h"
#include "global.h"
#include "window.h"

GLFWwindow *window;
struct global *k;

void window_title_callback(char *title)
{
        glfwSetWindowTitle(window, title);
}

int main(int argc, char **argv, char **env)
{
        (void)argc; /* TODO: Parse arguments. */

        if (!glfwInit()) return 1;

        /* TODO: Make the default window size configurable. */
        window = glfwCreateWindow(300, 300, argv[0], 0, 0);

        if (!window) {
                glfwTerminate();
                return 1;
        }

        glfwMakeContextCurrent(window);

        glfwSetCharCallback(window, character_callback);
        glfwSetKeyCallback(window, key_callback);
        glfwSetWindowSizeCallback(window, window_size_callback);

        k = calloc(1, sizeof *k);
        global_init(k, env, window_title_callback);

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
        if (global_load_fonts(k)) return 1;

        int width, height;
        glfwGetWindowSize(window, &width, &height);
        window_size_callback(window, width, height);

        /* Create the VBO, shader program, etc. */
        if (global_init_gl_resources(k)) return 1;

        pthread_t shell_reader;
        pthread_create(&shell_reader, NULL, read_shell, k);

        global_render(k);
        glfwSwapBuffers(window);

        while (!glfwWindowShouldClose(window) && !k->frame[0]->shell_done) {
                global_render(k);
                glfwSwapBuffers(window);
                glfwPollEvents();
        }

        glfwTerminate();

        return 0;
}
