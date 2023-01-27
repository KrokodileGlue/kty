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

enum debug_level debug_level = LOG_EVERYTHING;

int main(int argc, char **argv)
{
        (void)argc;

        struct layout_engine *e = layout_engine_create();
        if (!e) return 1;

        char *pattern[] = {
                "Fira Code:regular",
                /* "Fira Code:bold", */
                /* "Fira Code:italic", */
                /* "Fira Code:bold:italic", */
                /* "emoji", */
                /* "Noto Serif CJK JP", */
                /* "Noto Sans Arabic", */
        };

        for (unsigned i = 0; i < sizeof pattern / sizeof *pattern; i++)
                if (layout_engine_add_font_from_name(e, pattern[i], 12)) {
                        fprintf(stderr, "Couldn't load font %s\n", pattern[i]);
                        return 1;
                }

        struct cpu_cell *cells = calloc(1000, sizeof *cells);
        unsigned num_glyph = 0;

        /* Add all printable ASCII characters. */
        for (int i = '!'; i <= '~'; i++)
                cells[num_glyph++] = (struct cpu_cell){
                        .c = { i },
                        .num_code_point = 1,
                };

        cells[num_glyph++] = (struct cpu_cell){
                .c = { 0x201c },
                .num_code_point = 1,
        };

        struct glyph *glyphs = calloc(num_glyph, sizeof *glyphs);

        layout(e, cells, glyphs, num_glyph, 48);
        layout_engine_show(e);

        if (!glfwInit()) return 1;

        int width = 800, height = 602;
        int cw = 30, ch = 52;
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
        char *buf = malloc(10000);

        /* Vertex shader. */
        int fd = open("vertex.glsl", O_RDONLY);
        int n = read(fd, buf, 10000);
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
        n = read(fd, buf, 10000);
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

        /*  1,4   6
         *   +---+
         *   |   |
         *   |   |
         *   +---+
         *  3     2,5
          */
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

        struct glyph_manager *m = layout_engine_get_glyph_manager(e);

        /* There's only one glyph sheet. */
        struct glyph_sheet sheet = glyph_manager_get_glyph_sheet(m, 0);

        unsigned texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, sheet.width, sheet.height,
                     0, GL_RED, GL_UNSIGNED_BYTE, sheet.data);

        glUniform1i(glGetUniformLocation(program, "width"), width);
        glUniform1i(glGetUniformLocation(program, "height"), height);

        glUniform1i(glGetUniformLocation(program, "cw"), cw);
        glUniform1i(glGetUniformLocation(program, "ch"), ch);

        glUniform1i(glGetUniformLocation(program, "col"), col);
        glUniform1i(glGetUniformLocation(program, "row"), row);

        glUniform1i(glGetUniformLocation(program, "spritemap"), 0);

        for (unsigned i = 0; i < num_glyph; i++) {
                char tmp[1000];
                snprintf(tmp, sizeof tmp, "glyph_position[%d]", i);
                glUniform2i(glGetUniformLocation(program, tmp), glyphs[i].bitmap_left, glyphs[i].bitmap_top);
                snprintf(tmp, sizeof tmp, "glyph_vertex[%d]", i);
                glUniform2f(glGetUniformLocation(program, tmp),
                            (float)glyphs[i].size.x / (float)cw,
                            -(float)glyphs[i].size.y / (float)ch);
                snprintf(tmp, sizeof tmp, "glyph_texcoords[%d]", i);
                glUniform4f(glGetUniformLocation(program, tmp),
                            (float)glyphs[i].sprite_coordinates[0].x / (float)sheet.width,
                            (float)glyphs[i].sprite_coordinates[1].y / (float)sheet.height,
                            (float)glyphs[i].sprite_coordinates[1].x / (float)sheet.width,
                            (float)glyphs[i].sprite_coordinates[0].y / (float)sheet.height);
        }

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glClearColor(0.25, 0.5, 0.75, 1.0);

        /* https://www.khronos.org/opengl/wiki/Shader_Storage_Buffer_Object */

        /* Begin the main loop. */

        glfwSwapBuffers(window);

        while (!glfwWindowShouldClose(window)) {
                glClear(GL_COLOR_BUFFER_BIT);
                glDrawArraysInstanced(GL_TRIANGLES, 0, 6, num_glyph);
                glfwSwapBuffers(window);
                glfwPollEvents();
        }

        glfwTerminate();
}

/*
 * 1. Move the bitmap_top into the glyph rather than the gpu_cell. DONE
 *
 * 2. The gpu_cell should only have a glyph index, and fg/bg colors.
 *
 * 3. There should be an array of glyphs in the vertex shader which
 * each contains a few different fields, including the coordinates in
 * the sprite sheet and the bitmap_top.
 *
 * 4. There should be an array of gpu_cells using the same technique as (3).
 *
 * 5. Both the glyph array and the gpu_cell array in the shader should
 * use a shader storage buffer object.
 */
