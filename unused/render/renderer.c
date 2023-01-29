#include <GL/glew.h>

#include "renderer.h"
#include "layout/utf8.h"

#define FONT_SIZE 28

const char *vertex_shader_source = "\n\
#version 330 core\n\
\n\
in vec2 aPos;\n\
\n\
uniform int width;\n\
uniform int height;\n\
\n\
uniform int cw;\n\
uniform int ch;\n\
\n\
uniform int row;\n\
uniform int col;\n\
\n\
uniform int ascender;\n\
\n\
uniform ivec2 glyph_position[1000];\n\
uniform vec2 glyph_vertex[1000];\n\
uniform vec4 glyph_texcoords[1000];\n\
uniform int glyph_is_color[1000];\n\
uniform int glyph_ascender[1000];\n\
uniform int glyph_tex[1000];\n\
\n\
uniform int glyph_indices[4096];\n\
\n\
out vec3 out_color;\n\
out vec2 out_tex_coords;\n\
flat out int tex;\n\
flat out int is_color;\n\
\n\
// gl_VertexID\n\
\n\
void main()\n\
{\n\
        float sx = 2.0 / width;\n\
        float sy = 2.0 / height;\n\
\n\
        vec2 tvertex = vec2(0.0);\n\
\n\
        int glyph_index = glyph_indices[gl_InstanceID];\n\
\n\
        vec2 tnw = vec2(glyph_texcoords[glyph_index][0], glyph_texcoords[glyph_index][1]);\n\
        vec2 tse = vec2(glyph_texcoords[glyph_index][2], glyph_texcoords[glyph_index][3]);\n\
\n\
        int index = gl_VertexID % 6;\n\
\n\
        if (index == 0) {\n\
                tvertex = tnw;\n\
        } else if (index == 1) {\n\
                tvertex = tse;\n\
        } else if (index == 2) {\n\
                tvertex = vec2(tnw.x, tse.y);\n\
        } else if (index == 3) {\n\
                tvertex = tnw;\n\
        } else if (index == 4) {\n\
                tvertex = tse;\n\
        } else if (index == 5) {\n\
                tvertex = vec2(tse.x, tnw.y);\n\
        }\n\
\n\
        vec2 vertex = aPos * glyph_vertex[glyph_index];\n\
\n\
        vec2 offset = vec2((gl_InstanceID % col) * cw * sx - 1.0,\n\
                           (gl_InstanceID / col + 1) * -ch * sy + 1.0);\n\
        offset += vec2(sx, sy) * glyph_position[glyph_index];\n\
        if (glyph_ascender[glyph_index] != 0) {\n\
                offset.y += sy * -glyph_ascender[glyph_index] + -sy * ascender / 2;\n\
        } else {\n\
                offset.y += -sy * ascender;\n\
        }\n\
        gl_Position = vec4(vec2(vertex.x * cw * sx, vertex.y * ch * sy) + offset, 0.0, 1.0);\n\
        out_tex_coords = tvertex;\n\
        tex = glyph_tex[glyph_index];\n\
        is_color = glyph_is_color[glyph_index];\n\
}\n\
";

const char *fragment_shader_source = "\n\
#version 330 core\n\
\n\
in vec2 out_tex_coords;\n\
flat in int tex;\n\
flat in int is_color;\n\
\n\
out vec4 FragColor;\n\
\n\
uniform sampler2DArray color_textures;\n\
uniform sampler2DArray alpha_textures;\n\
\n\
void main()\n\
{\n\
        vec3 color = vec3(1.0, 1.0, 1.0);\n\
\n\
        if (is_color == 1) {\n\
                FragColor = texture(color_textures, vec3(out_tex_coords, tex)).bgra;\n\
        } else {\n\
                FragColor = vec4(color, texture(alpha_textures, vec3(out_tex_coords, tex)));\n\
        }\n\
        //FragColor = vec4(1.0, 1.0, 1.0, 0.5);\n\
}\n\
";

static void
upload_glyphs(struct renderer *r, struct basic_font_info info)
{
        struct glyph_manager *m = layout_engine_get_glyph_manager(r->e);

        /* There's only one glyph sheet. */
        struct glyph_sheet sheet = glyph_manager_get_glyph_sheet(m, 0);

        for (unsigned i = 0; i < m->num_glyph; i++) {
                char tmp[1000];

                snprintf(tmp, sizeof tmp, "glyph_position[%d]", i);
                glUniform2i(glGetUniformLocation(r->program, tmp),
                            m->glyph[i].bitmap_left,
                            m->glyph[i].bitmap_top);

                snprintf(tmp, sizeof tmp, "glyph_tex[%d]", i);
                glUniform1i(glGetUniformLocation(r->program, tmp),
                            m->glyph[i].glyph_sheet);

                int glyph_sheet = m->glyph[i].glyph_sheet;
                struct glyph_sheet sheet = glyph_manager_get_glyph_sheet(m, glyph_sheet);
                snprintf(tmp, sizeof tmp, "glyph_is_color[%d]", i);
                glUniform1i(glGetUniformLocation(r->program, tmp),
                            sheet.format == GLYPH_SHEET_COLOR);

                snprintf(tmp, sizeof tmp, "glyph_vertex[%d]", i);
                glUniform2f(glGetUniformLocation(r->program, tmp),
                            (float)m->glyph[i].size.x / (float)info.cw,
                            -(float)m->glyph[i].size.y / (float)info.ch);

                snprintf(tmp, sizeof tmp, "glyph_ascender[%d]", i);
                glUniform1i(glGetUniformLocation(r->program, tmp), m->glyph[i].ascender);

                snprintf(tmp, sizeof tmp, "glyph_texcoords[%d]", i);
                glUniform4f(glGetUniformLocation(r->program, tmp),
                            (float)m->glyph[i].sprite_coordinates[0].x / (float)sheet.width,
                            (float)m->glyph[i].sprite_coordinates[1].y / (float)sheet.height,
                            (float)m->glyph[i].sprite_coordinates[1].x / (float)sheet.width,
                            (float)m->glyph[i].sprite_coordinates[0].y / (float)sheet.height);
        }

        /* Now upload the textures. */

        GLsizei width = sheet.width;
        GLsizei height = sheet.height;
        GLsizei mipLevelCount = 1;

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

        int alpha_texture_location = glGetUniformLocation(r->program, "alpha_textures");
        if (alpha_texture_location == GL_INVALID_VALUE || alpha_texture_location == GL_INVALID_OPERATION)
                printf("alpha texture uniform not found: %d\n", alpha_texture_location);

        int color_texture_location = glGetUniformLocation(r->program, "color_textures");
        if (color_texture_location == GL_INVALID_VALUE || color_texture_location == GL_INVALID_OPERATION)
                printf("color texture uniform not found: %d\n", color_texture_location);

        glUniform1i(alpha_texture_location, 0);
        glUniform1i(color_texture_location, 1);
}

int
renderer_render_text(struct renderer *r, const char *s)
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
        layout(r->e, cells, num_cells, glyphs, &num_glyphs, FONT_SIZE);

        for (unsigned i = 0; i < num_glyphs; i++)
                arr[i] = glyphs[i].index;

        glUniform1iv(glGetUniformLocation(r->program, "glyph_indices"), num_glyphs, arr);

        struct basic_font_info info;
        if (layout_engine_get_basic_font_info(r->e, &info, FONT_SIZE)) {
                fprintf(stderr, "Couldn't get basic information about font");
                return 1;
        }

        upload_glyphs(r, info);

        return num_glyphs;
}

int
renderer_init(struct renderer *r)
{
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

        r->e = layout_engine_create();
        if (!r->e) return 1;

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
                if (layout_engine_add_font_from_name(r->e, pattern[i], 12)) {
                        fprintf(stderr, "Couldn't load font %s\n", pattern[i]);
                        return 1;
                }

        struct basic_font_info info;
        if (layout_engine_get_basic_font_info(r->e, &info, FONT_SIZE)) {
                fprintf(stderr, "Couldn't get basic information about font");
                return 1;
        }

        int ascender = info.ascender;
        int cw = info.cw, ch = info.ch;
        int col = 800 / cw, row = 602 / ch;

        /* Vertex shader. */
        unsigned vertex_shader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertex_shader, 1, &vertex_shader_source, NULL);
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
        unsigned int fragment_shader;
        fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragment_shader, 1, &fragment_shader_source, NULL);
        glCompileShader(fragment_shader);

        glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);

        if (!success) {
                glGetShaderInfoLog(fragment_shader, sizeof infoLog, NULL, infoLog);
                puts("ERROR::SHADER::FRAGMENT::COMPILATION_FAILED");
                puts(infoLog);
                exit(1);
        }

        /* Create the program. */
        r->program = glCreateProgram();
        glAttachShader(r->program, vertex_shader);
        glAttachShader(r->program, fragment_shader);
        glLinkProgram(r->program);

        glGetProgramiv(r->program, GL_LINK_STATUS, &success);

        if (!success) {
                glGetProgramInfoLog(r->program, sizeof infoLog, NULL, infoLog);
                puts("linking failed");
                puts(infoLog);
                exit(1);
        }

        glUseProgram(r->program);

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

        glUniform1i(glGetUniformLocation(r->program, "ascender"), ascender);

        glUniform1i(glGetUniformLocation(r->program, "width"), 800);
        glUniform1i(glGetUniformLocation(r->program, "height"), 602);

        glUniform1i(glGetUniformLocation(r->program, "cw"), cw);
        glUniform1i(glGetUniformLocation(r->program, "ch"), ch);

        glUniform1i(glGetUniformLocation(r->program, "col"), col);
        glUniform1i(glGetUniformLocation(r->program, "row"), row);

        return 0;
}
