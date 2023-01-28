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

#define FONT_SIZE 16

enum debug_level debug_level = LOG_EVERYTHING;
unsigned program;

static void
upload_glyphs(struct layout_engine *e, struct basic_font_info info)
{
        struct glyph_manager *m = layout_engine_get_glyph_manager(e);

        /* There's only one glyph sheet. */
        struct glyph_sheet sheet = glyph_manager_get_glyph_sheet(m, 0);

        for (unsigned i = 0; i < m->num_glyph; i++) {
                char tmp[1000];

                snprintf(tmp, sizeof tmp, "glyph_position[%d]", i);
                glUniform2i(glGetUniformLocation(program, tmp),
                            m->glyph[i].bitmap_left,
                            m->glyph[i].bitmap_top);

                snprintf(tmp, sizeof tmp, "glyph_tex[%d]", i);
                glUniform1i(glGetUniformLocation(program, tmp),
                            m->glyph[i].glyph_sheet);

                int glyph_sheet = m->glyph[i].glyph_sheet;
                struct glyph_sheet sheet = glyph_manager_get_glyph_sheet(m, glyph_sheet);
                snprintf(tmp, sizeof tmp, "glyph_is_color[%d]", i);
                glUniform1i(glGetUniformLocation(program, tmp),
                            sheet.format == GLYPH_SHEET_COLOR);

                snprintf(tmp, sizeof tmp, "glyph_vertex[%d]", i);
                glUniform2f(glGetUniformLocation(program, tmp),
                            (float)m->glyph[i].size.x / (float)info.cw,
                            -(float)m->glyph[i].size.y / (float)info.ch);

                snprintf(tmp, sizeof tmp, "glyph_ascender[%d]", i);
                glUniform1i(glGetUniformLocation(program, tmp), m->glyph[i].ascender);

                snprintf(tmp, sizeof tmp, "glyph_texcoords[%d]", i);
                glUniform4f(glGetUniformLocation(program, tmp),
                            (float)m->glyph[i].sprite_coordinates[0].x / (float)sheet.width,
                            (float)m->glyph[i].sprite_coordinates[1].y / (float)sheet.height,
                            (float)m->glyph[i].sprite_coordinates[1].x / (float)sheet.width,
                            (float)m->glyph[i].sprite_coordinates[0].y / (float)sheet.height);
        }

        /* Now upload the textures. */

        GLsizei width = sheet.width;
        GLsizei height = sheet.height;
        GLsizei mipLevelCount = 1;

        int num_alpha = 0, num_color = 0;

        for (unsigned i = 0; i < m->num_sprite_map; i++) {
                if (m->sprite_map[i]->cairo_format == CAIRO_FORMAT_A8) num_alpha++;
                else num_color++;
        }

        GLuint alpha_texture;
        glGenTextures(1, &alpha_texture);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, alpha_texture);
        glTexStorage3D(GL_TEXTURE_2D_ARRAY, mipLevelCount, GL_R8, width, height, m->num_sprite_map);

        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        for (unsigned i = 0; i < m->num_sprite_map; i++) {
                if (m->sprite_map[i]->cairo_format != CAIRO_FORMAT_A8) continue;
                struct glyph_sheet sheet = glyph_manager_get_glyph_sheet(m, i);
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, width, height, 1, GL_RED, GL_UNSIGNED_BYTE, sheet.data);
        }

        GLuint color_texture;
        glGenTextures(1, &color_texture);
        glActiveTexture(GL_TEXTURE0 + 1);
        glBindTexture(GL_TEXTURE_2D_ARRAY, color_texture);
        glTexStorage3D(GL_TEXTURE_2D_ARRAY, mipLevelCount, GL_RGBA8, width, height, m->num_sprite_map);

        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        for (unsigned i = 0; i < m->num_sprite_map; i++) {
                if (m->sprite_map[i]->cairo_format == CAIRO_FORMAT_A8) continue;
                struct glyph_sheet sheet = glyph_manager_get_glyph_sheet(m, i);
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE, sheet.data);
        }

        int alpha_texture_location = glGetUniformLocation(program, "alpha_textures");
        if (alpha_texture_location == GL_INVALID_VALUE || alpha_texture_location == GL_INVALID_OPERATION)
                printf("alpha texture uniform not found: %d\n", alpha_texture_location);

        int color_texture_location = glGetUniformLocation(program, "color_textures");
        if (color_texture_location == GL_INVALID_VALUE || color_texture_location == GL_INVALID_OPERATION)
                printf("color texture uniform not found: %d\n", color_texture_location);

        glUniform1i(alpha_texture_location, 0);
        glUniform1i(color_texture_location, 1);
}

static int
render_text(struct layout_engine *e, const char *s)
{
        static int arr[1000];
        static struct cpu_cell cells[1000];

        int num_cells = 0;

        for (unsigned i = 0; i < strlen(s); i++) {
                unsigned len = utf8chrlen(s + i, strlen(s) - i);
                uint32_t c;
                utf8decode(s + i, len, &c);

                cells[num_cells++] = (struct cpu_cell){
                        .c = { c },
                        .num_code_point = 1,
                };

                i += len - 1;
        }

        struct glyph *glyphs = calloc(2 * num_cells, sizeof *glyphs);

        unsigned num_glyphs = 0;
        layout(e, cells, num_cells, glyphs, &num_glyphs, FONT_SIZE);

        for (unsigned i = 0; i < num_glyphs; i++)
                arr[i] = glyphs[i].index;

        glUniform1iv(glGetUniformLocation(program, "glyph_indices"), num_glyphs, arr);

        return num_glyphs;
}

int main(int argc, char **argv)
{
        (void)argc;

        struct layout_engine *e = layout_engine_create();
        if (!e) return 1;

        char *pattern[] = {
                "Fira Code:regular",
                "Fira Code:bold",
                "Fira Code:italic",
                "Fira Code:bold:italic",
                "emoji",
                "Noto Serif CJK JP",
                "Noto Sans Arabic",
        };

        for (unsigned i = 0; i < sizeof pattern / sizeof *pattern; i++)
                if (layout_engine_add_font_from_name(e, pattern[i], 12)) {
                        fprintf(stderr, "Couldn't load font %s\n", pattern[i]);
                        return 1;
                }

        if (!glfwInit()) return 1;

        int width = 800, height = 602;

        struct basic_font_info info;
        layout_engine_get_basic_font_info(e, &info, FONT_SIZE);

        int ascender = info.ascender;
        int cw = info.cw, ch = info.ch;
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
        program = glCreateProgram();
        glAttachShader(program, vertex_shader);
        glAttachShader(program, fragment_shader);
        glLinkProgram(program);

        glGetProgramiv(program, GL_LINK_STATUS, &success);

        if (!success) {
                glGetProgramInfoLog(program, sizeof infoLog, NULL, infoLog);
                puts("linking failed");
                puts(infoLog);
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

        glUniform1i(glGetUniformLocation(program, "ascender"), ascender);

        glUniform1i(glGetUniformLocation(program, "width"), width);
        glUniform1i(glGetUniformLocation(program, "height"), height);

        glUniform1i(glGetUniformLocation(program, "cw"), cw);
        glUniform1i(glGetUniformLocation(program, "ch"), ch);

        glUniform1i(glGetUniformLocation(program, "col"), col);
        glUniform1i(glGetUniformLocation(program, "row"), row);

        glUniform1i(glGetUniformLocation(program, "spritemap"), 0);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glClearColor(0, 0, 0, 1.0);

        /* https://www.khronos.org/opengl/wiki/Shader_Storage_Buffer_Object */

        /* Begin the main loop. */

        glfwSwapBuffers(window);

        int num_glyph = render_text(e, "https://www.khronos.org/opengl/wiki/Shader_Storage_Buffer_Object what's up :) => ---- xD - honk ! <🤔 > as well as perhaps a (😎) Lorem Ipsum is simply dummy text of the printing and typesetting industry. Lorem Ipsum has been the industry's standard 🤣 dummy text ever since the 1500s, when an unknown printer took a galley of type and scrambled it to make a type specimen book. It has survived not only five centuries, but also the leap into electronic typesetting, remaining essentially unchanged. It was popularised in the 1960s with the release of Letraset 🤔 sheets containing Lorem Ipsum passages, and more recently with desktop publishing software like Aldus PageMaker including versions of Lorem Ipsum. <=> -> بلغة يونيكود. تسجّل الآن لحضور المؤتمر ");
        /* int num_glyph = render_text(e, "https://www.khronos.org/opengl/wiki/Shader_Storage_Buffer_Object what's up :) => ---- xD - honk ! <🤔 > as well as perhaps a (😎) Lorem Ipsum is simply dummy text of the printing and typesetting industry. Lorem Ipsum has been the industry's standard 🤣 dummy text ever since the 1500s, when an unknown printer took a galley of type and scrambled it to make a type specimen book. It has survived not only five centuries, but also the leap into electronic typesetting, remaining essentially unchanged. It was popularised in the 1960s with the release of Letraset 🤔 sheets containing Lorem Ipsum passages, and more recently with desktop publishing software like Aldus PageMaker including versions of Lorem Ipsum. <=> ->"); */
        upload_glyphs(e, info);

        while (!glfwWindowShouldClose(window)) {
                glClear(GL_COLOR_BUFFER_BIT);
                glDrawArraysInstanced(GL_TRIANGLES, 0, 6, num_glyph);
                glfwSwapBuffers(window);
                glfwWaitEventsTimeout(0.5);
        }

        glfwTerminate();
}
