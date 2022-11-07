#include <stdlib.h>

#include <GLFW/glfw3.h>

int main(void)
{
        if (!glfwInit()) return 1;

        GLFWwindow *window = glfwCreateWindow(600, 700, "Hello, world!", NULL, NULL);

        if (!window) {
                glfwTerminate();
                return 1;
        }

        glfwMakeContextCurrent(window);

        while (!glfwWindowShouldClose(window)) {
                glClear(GL_COLOR_BUFFER_BIT);
                glfwSwapBuffers(window);
                glfwPollEvents();
        }

        glfwTerminate();
        return 0;
}
