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

enum debug_level debug_level = LOG_CRITICAL;

int main(int argc, char **argv)
{
        (void)argc;

        if (!glfwInit()) return 1;

        GLFWwindow *window = glfwCreateWindow(800, 602, argv[0], 0, 0);

        if (!window) {
                glfwTerminate();
                exit(1);
        }

        glfwMakeContextCurrent(window);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glClearColor(0, 0, 0, 1.0);

        glfwSwapBuffers(window);

        struct renderer *r = calloc(1, sizeof *r);
        if (renderer_init(r)) {
                fprintf(stderr, "Couldn't initialize renderer");
                return 1;
        }

        int num_glyph = renderer_render_text(r, "https://www.khronos.org/opengl/wiki/Shader_Storage_Buffer_Object what's up :) => ---- xD - honk ! <🤔 > as well as perhaps a (😎) Lorem Ipsum is simply dummy text of the printing and typesetting industry. Lorem Ipsum has been the industry's standard 🤣 dummy text ever since the 1500s, when an unknown printer took a galley of type and scrambled it to make a type specimen book. It has survived not only five centuries, but also the leap into electronic typesetting, remaining essentially unchanged. It was popularised in the 1960s with the release of Letraset 🤔 sheets containing Lorem Ipsum passages, and more recently with desktop publishing software like Aldus PageMaker including versions of Lorem Ipsum. <=> -> بلغة يونيكود. تسجّل الآن لحضور المؤتمر ");

        while (!glfwWindowShouldClose(window)) {
                glClear(GL_COLOR_BUFFER_BIT);
                glDrawArraysInstanced(GL_TRIANGLES, 0, 6, num_glyph);
                glfwSwapBuffers(window);
                glfwWaitEventsTimeout(0.5);
        }

        glfwTerminate();
}
