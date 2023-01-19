#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

int main(int argc, char **argv)
{
        (void)argc;

        if (!glfwInit()) return 1;

        int width = 800, height = 602;
        int cw = 14, ch = 24;
        int col = width / cw, row = height / ch;

        GLFWwindow *window = glfwCreateWindow(width, height, argv[0], 0, 0);
        if (!window) {
                glfwTerminate();
                exit(1);
        }

        glfwMakeContextCurrent(window);

        /* Initialize GLEW. */
        glewExperimental = GL_TRUE;
        GLenum glew_status = glewInit();

        if (GLEW_OK != glew_status) {
                fprintf(stderr, "Error: %s\n", glewGetErrorString(glew_status));
                exit(1);
        }

        if (!GLEW_VERSION_3_3) {
                fprintf(stderr, "No support for OpenGL 3.3 found\n");
                exit(1);
        }

        /* Start setting up the renderer. */

        /* Compile shaders. */
        char *buf = malloc(1000);

        /* Vertex shader. */
        int fd = open("vertex.glsl", O_RDONLY);
        int n = read(fd, buf, 1000);
        buf[n] = 0;
        close(fd);

        unsigned vertex_shader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertex_shader, 1, &buf, NULL);
        glCompileShader(vertex_shader);

        int success;
        char infoLog[512];
        glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);

        if (!success) {
                glGetShaderInfoLog(vertex_shader, sizeof infoLog, NULL, infoLog);
                puts("ERROR::SHADER::VERTEX::COMPILATION_FAILED");
                puts(infoLog);
                exit(1);
        }

        /* Fragment shader. */
        fd = open("fragment.glsl", O_RDONLY);
        n = read(fd, buf, 1000);
        buf[n] = 0;
        close(fd);

        unsigned int fragment_shader;
        fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragment_shader, 1, &buf, NULL);
        glCompileShader(fragment_shader);

        glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);

        if (!success) {
                glGetShaderInfoLog(fragment_shader, sizeof infoLog, NULL, infoLog);
                puts("ERROR::SHADER::FRAGMENT::COMPILATION_FAILED");
                puts(infoLog);
                exit(1);
        }

        /* Create the program. */
        unsigned program = glCreateProgram();
        glAttachShader(program, vertex_shader);
        glAttachShader(program, fragment_shader);
        glLinkProgram(program);

        glGetProgramiv(program, GL_LINK_STATUS, &success);

        if (!success) {
                glGetProgramInfoLog(program, 512, NULL, buf);
                puts("linking failed");
                exit(1);
        }

        glUseProgram(program);
        /* Create the uniforms. */

        struct vec2 { float x, y; };

        struct vec2 vertices[] = {
                { 0, 1 },
                { 1, 0 },
                { 0, 0 },
                { 0, 1 },
                { 1, 0 },
                { 1, 1 },
        };

        unsigned VBO;
        glGenBuffers(1, &VBO);

        unsigned VAO;
        glGenVertexArrays(1, &VAO);
        glBindVertexArray(VAO);

        glEnableVertexAttribArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof vertices, vertices, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof *vertices, 0);

        /* int index = 0; */

        /* for (int y = -10; y < 10; y += 2) { */
        /*         for (int x = -10; x < 10; x += 2) { */
        /*                 char tmp[1000]; */
        /*                 snprintf(tmp, sizeof tmp, "offsets[%d]", index++); */
        /*                 glUniform2f(glGetUniformLocation(program, tmp), */
        /*                             (float)x / 10.0f + 0.1, */
        /*                             (float)y / 10.0f + 0.1); */
        /*         } */
        /* } */

        glUniform1i(glGetUniformLocation(program, "width"), width);
        glUniform1i(glGetUniformLocation(program, "height"), height);

        glUniform1i(glGetUniformLocation(program, "cw"), cw);
        glUniform1i(glGetUniformLocation(program, "ch"), ch);

        glUniform1i(glGetUniformLocation(program, "col"), col);
        glUniform1i(glGetUniformLocation(program, "row"), row);

        /* Begin the main loop. */

        glfwSwapBuffers(window);

        while (!glfwWindowShouldClose(window)) {
                glDrawArraysInstanced(GL_TRIANGLES, 0, 6, row * col);
                glfwSwapBuffers(window);
                glfwPollEvents();
        }

        glfwTerminate();
}
