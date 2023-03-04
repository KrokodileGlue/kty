#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <math.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "layout/layout_engine.h"
#include "layout/debug.h"
#include "layout/utf8.h"

#include "renderer.h"
#include "canvas.h"

enum debug_level debug_level = LOG_DETAIL;

int main(int argc, char **argv)
{
        (void)argc;

        glfwInit();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        GLFWwindow *window = glfwCreateWindow(600, 600, argv[0], 0, 0);

        if (!window) {
                glfwTerminate();
                return 1;
        }

        glfwMakeContextCurrent(window);

        struct renderer *renderer = calloc(1, sizeof *renderer);

        if (renderer_init(renderer)) {
                print(LOG_CRITICAL, "Couldn't initialize renderer\n");
                return 1;
        }

        struct canvas *canvas = calloc(1, sizeof *canvas);

        if (canvas_init(canvas, renderer)) {
                print(LOG_CRITICAL, "Couldn't initialize canvas\n");
                return 1;
        }

        canvas_resize(canvas, 600, 600, 28);
        /* canvas_render_string(canvas, 0, 0, "https://www.khronos.org/opengl/wiki/Shader_Storage_Buffer_Object what's up :) => ---- xD - honk ! <🤔 > as well as perhaps a (😎) Lorem Ipsum is simply dummy text of the printing and typesetting industry. Lorem Ipsum has been the industry's standard 🤣 dummy text ever since the 1500s, when an unknown printer took a galley of type and scrambled it to make a type specimen book. It has survived not only five centuries, but also the leap into electronic typesetting, remaining essentially unchanged. It was popularised in the 1960s with the release of Letraset 🤔 sheets containing Lorem Ipsum passages, and more recently with desktop publishing software like Aldus PageMaker including versions of Lorem Ipsum. <=> -> بلغة يونيكود. تسجّل الآن لحضور المؤتمر "); */
        canvas_render_string(canvas, 0, 0, "abcd");

        while (!glfwWindowShouldClose(window)) {
                renderer_render_canvas(renderer, canvas);
                glfwSwapBuffers(window);
                glfwWaitEventsTimeout(0.5);
        }

        glfwTerminate();
}
