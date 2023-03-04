#include <GL/glew.h>

#include "renderer.h"
#include "canvas.h"
#include "layout/utf8.h"
#include "layout/debug.h"

static void GLAPIENTRY
MessageCallback(GLenum source,
                GLenum type,
                GLuint id,
                GLenum severity,
                GLsizei length,
                const GLchar *message,
                const void *userParam)
{
        (void)source, (void)id, (void)length, (void)userParam;
        print(type == GL_DEBUG_TYPE_ERROR ? LOG_CRITICAL : LOG_INFORMATION,
              "type = 0x%x, severity = 0x%x, message = %s\n",
              type, severity, message);
}

static const char *vertex_shader_source = "\n\
#version 330 core\n\
\n\
in vec2 aPos;\n\
\n\
layout(std140) uniform glyph_position_block  { ivec2 glyph_position[4096];  };\n\
layout(std140) uniform glyph_vertex_block    { ivec2 glyph_vertex[4096];    };\n\
layout(std140) uniform glyph_texcoords_block { vec4  glyph_texcoords[4096]; };\n\
layout(std140) uniform glyph_is_color_block  { int   glyph_is_color[4096];  };\n\
layout(std140) uniform glyph_ascender_block  { int   glyph_ascender[4096];  };\n\
layout(std140) uniform glyph_tex_block       { int   glyph_tex[4096];       };\n\
\n\
out vec2 tex_coordinates;\n\
\n\
void main() {\n\
	tex_coordinates = aPos;\n\
	gl_Position = vec4(1.0 * (aPos - 0.5), 0.0, 1.0);\n\
}\n\
";

static const char *fragment_shader_source = "\n\
#version 330 core\n\
\n\
in vec2 tex_coordinates;\n\
\n\
out vec4 out_color;\n\
\n\
uniform sampler2DArray color_textures;\n\
uniform sampler2DArray alpha_textures;\n\
uniform sampler2D tex;\n\
\n\
void main() {\n\
	out_color = texture(tex, tex_coordinates) + 0.2;\n\
}\n\
";

static int
attach_shader_program(struct renderer *r)
{
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

        return 0;
}

static int
make_uniform_buffer_object(struct renderer *r, const char *name, unsigned size)
{
        unsigned block_index = glGetUniformBlockIndex(r->program, name);

        if (block_index == (unsigned)-1) {
                print(LOG_CRITICAL, "Couldn't find block index for uniform buffer object `%s'\n", name);
                return -1;
        }

        unsigned block;
        glGenBuffers(1, &block);
        glBindBuffer(GL_UNIFORM_BUFFER, block);
        glBufferData(GL_UNIFORM_BUFFER, size, NULL, GL_STREAM_DRAW);

        static unsigned binding_index = 0;
        binding_index++;
        glUniformBlockBinding(r->program, block_index, binding_index);

        print(LOG_DETAIL, "Found index %d for uniform buffer object `%s'\n",
              block_index, name);

        return block;
}

static void
upload_glyphs(struct renderer *r)
{
        struct glyph_manager *m = layout_engine_get_glyph_manager(r->e);

        /* There's only one glyph sheet. */
        struct glyph_sheet sheet = glyph_manager_get_glyph_sheet(m, 0);

        glUseProgram(r->program);

        unsigned glyph_position_block  = make_uniform_buffer_object(r, "glyph_position_block",  4096 * 2 * sizeof (int));
        unsigned glyph_vertex_block    = make_uniform_buffer_object(r, "glyph_vertex_block",    4096 * 2 * sizeof (int));
        unsigned glyph_texcoords_block = make_uniform_buffer_object(r, "glyph_texcoords_block", 4096 * 4 * sizeof (float));
        unsigned glyph_is_color_block  = make_uniform_buffer_object(r, "glyph_is_color_block",  4096 * sizeof (int));
        unsigned glyph_ascender_block  = make_uniform_buffer_object(r, "glyph_ascender_block",  4096 * sizeof (int));
        unsigned glyph_tex_block       = make_uniform_buffer_object(r, "glyph_tex_block",       4096 * sizeof (int));

        for (unsigned i = 0; i < m->num_glyph; i++) {
                print(LOG_DETAIL, "Uploading glyph %u (%d %d)\n", i, m->glyph[i].bitmap_left, m->glyph[i].bitmap_top);

                glBindBuffer(GL_UNIFORM_BUFFER, glyph_position_block);
                glBufferSubData(GL_UNIFORM_BUFFER,
                                i * 2 * sizeof (int), /* offset */
                                2 * sizeof (int),     /* size */
                                (int []){ m->glyph[i].bitmap_left,
                                          m->glyph[i].bitmap_top });

                glBindBuffer(GL_UNIFORM_BUFFER, glyph_vertex_block);
                glBufferSubData(GL_UNIFORM_BUFFER,
                                i * 2 * sizeof (int), /* offset */
                                2 * sizeof (int),     /* size */
                                (int []){ m->glyph[i].size.x,
                                          -m->glyph[i].size.y });

                glBindBuffer(GL_UNIFORM_BUFFER, glyph_texcoords_block);
                glBufferSubData(GL_UNIFORM_BUFFER,
                                i * 4 * sizeof (float), /* offset */
                                4 * sizeof (float),     /* size */
                                (float []){ (float)m->glyph[i].sprite_coordinates[0].x / (float)sheet.width,
                                            (float)m->glyph[i].sprite_coordinates[1].y / (float)sheet.height,
                                            (float)m->glyph[i].sprite_coordinates[1].x / (float)sheet.width,
                                            (float)m->glyph[i].sprite_coordinates[0].y / (float)sheet.height });

                glBindBuffer(GL_UNIFORM_BUFFER, glyph_is_color_block);
                glBufferSubData(GL_UNIFORM_BUFFER,
                                i * sizeof (int), /* offset */
                                sizeof (int),     /* size */
                                (int []){ sheet.format == GLYPH_SHEET_COLOR });

                glBindBuffer(GL_UNIFORM_BUFFER, glyph_ascender_block);
                glBufferSubData(GL_UNIFORM_BUFFER,
                                i * sizeof (int), /* offset */
                                sizeof (int),     /* size */
                                (int []){ m->glyph[i].ascender });

                glBindBuffer(GL_UNIFORM_BUFFER, glyph_tex_block);
                glBufferSubData(GL_UNIFORM_BUFFER,
                                i * sizeof (int), /* offset */
                                sizeof (int),     /* size */
                                (int []){ m->glyph[i].glyph_sheet });
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

        printf("alpha texture: %u, color texture: %u\n", alpha_texture, color_texture);
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

        glEnable(GL_DEBUG_OUTPUT);
        glDebugMessageCallback(MessageCallback, 0);

        r->e = layout_engine_create();
        if (!r->e) return 1;

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
                if (layout_engine_add_font_from_name(r->e, pattern[i], 12)) {
                        fprintf(stderr, "Couldn't load font %s\n", pattern[i]);
                        return 1;
                }

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

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glClearColor(0, 1.0, 0, 1.0);

        attach_shader_program(r);

        return 0;
}

int
renderer_begin(struct renderer *r)
{
        (void)r;
        /* glBindFramebuffer(GL_FRAMEBUFFER, 0); */
        /* glClear(GL_COLOR_BUFFER_BIT); */
        return 0;
}

int
renderer_upload_glyphs(struct renderer *r)
{
        upload_glyphs(r);
        return 0;
}

int
renderer_render_canvas(struct renderer *r, struct canvas *c)
{
        /* static int frame = 0; */
        /* printf("%d - width=%d height=%d row=%d col=%d cw=%d ch=%d program=%u framebuffer=%u texture=%u\n", frame++, c->width, c->height, c->row, c->col, c->cw, c->ch, c->program, c->framebuffer, c->texture); */
        glBindFramebuffer(GL_FRAMEBUFFER, c->framebuffer);
        glUseProgram(c->program);
        /* glViewport(0, 0, c->width, c->height); */
        glClearColor(1, 0.5, 0.5, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, c->row * c->col);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glUseProgram(r->program);
        /* printf("tex %u -> %u\n", glGetUniformLocation(r->program, "tex"), c->texture); */
        glUniform1i(glGetUniformLocation(r->program, "tex"), c->texture);
        /* glViewport(0, 0, 600, 600); */
        glClearColor(1, 1, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glActiveTexture(GL_TEXTURE0 + 0);
        glBindTexture(GL_TEXTURE_2D, c->texture);
        glUniform1i(glGetUniformLocation(r->program, "tex"), 0);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        return 0;
}
