#include <GL/glew.h>

#include "canvas.h"
#include "layout/utf8.h"
#include "layout/debug.h"

static const char *vertex_shader_source = "\n\
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
layout(std140) uniform glyph_position_block  { ivec2 glyph_position[4096];  };\n\
layout(std140) uniform glyph_vertex_block    { ivec2 glyph_vertex[4096];    };\n\
layout(std140) uniform glyph_texcoords_block { vec4  glyph_texcoords[4096]; };\n\
layout(std140) uniform glyph_is_color_block  { int   glyph_is_color[4096];  };\n\
layout(std140) uniform glyph_ascender_block  { int   glyph_ascender[4096];  };\n\
layout(std140) uniform glyph_tex_block       { int   glyph_tex[4096];       };\n\
\n\
uniform int glyph_indices[4096];\n\
\n\
out vec3 out_color;\n\
out vec2 pos;\n\
out vec2 out_tex_coords;\n\
flat out int tex;\n\
flat out int is_color;\n\
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
        vec2 vertex = aPos * vec2(glyph_vertex[glyph_index].x / float(cw), glyph_vertex[glyph_index].y / float(ch));\n\
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
	gl_Position = vec4(aPos - 1.0 + vec2(sx, sy) * vec2(cw, ch) + gl_InstanceID / 2000.0, 0, 1.0);\n\
	pos = aPos;\n\
}\n\
";

static const char *fragment_shader_source = "\n\
#version 330 core\n\
\n\
in vec2 pos;\n\
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
        FragColor = vec4(pos, 0.0, 0.5);\n\
}\n\
";

static int
attach_shader_program(struct canvas *c)
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
        c->program = glCreateProgram();
        glAttachShader(c->program, vertex_shader);
        glAttachShader(c->program, fragment_shader);
        glLinkProgram(c->program);

        glGetProgramiv(c->program, GL_LINK_STATUS, &success);

        if (!success) {
                glGetProgramInfoLog(c->program, sizeof infoLog, NULL, infoLog);
                puts("linking failed");
                puts(infoLog);
                exit(1);
        }

        print(LOG_DETAIL, "Successfully compiled canvas shader\n");

        int alpha_texture_location = glGetUniformLocation(c->program, "alpha_textures");
        if (alpha_texture_location == GL_INVALID_VALUE || alpha_texture_location == GL_INVALID_OPERATION)
                fprintf(stderr, "alpha texture uniform not found: %d\n", alpha_texture_location);

        int color_texture_location = glGetUniformLocation(c->program, "color_textures");
        if (color_texture_location == GL_INVALID_VALUE || color_texture_location == GL_INVALID_OPERATION)
                fprintf(stderr, "color texture uniform not found: %d\n", color_texture_location);

        glUseProgram(c->program);

        print(LOG_DETAIL, "Alpha and color texture locations: %d %d\n", alpha_texture_location, color_texture_location);

        glUniform1i(alpha_texture_location, 2);
        glUniform1i(color_texture_location, 3);

        return 0;
}

static int
attach_framebuffer_to_canvas(struct canvas *c)
{
        glGenFramebuffers(1, &c->framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, c->framebuffer);

        /* Set up the texture */
        glGenTextures(1, &c->texture);
        glBindTexture(GL_TEXTURE_2D, c->texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        print(LOG_DETAIL, "Creating texture for canvas framebuffer with dimensions %d,%d\n", c->width, c->height);
        /* TODO: Don't use the window width and height here. */
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, c->width, c->height, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, NULL);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, c->texture, 0);

        GLuint status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

        if (status != GL_FRAMEBUFFER_COMPLETE) {
                print(LOG_CRITICAL, "Framebuffer status: %u\n", status);
                return 1;
        }

        print(LOG_DETAIL, "Framebuffer is complete\n");

        return 0;
}

int
canvas_init(struct canvas *c, struct renderer *r)
{
        c->r = r;
        if (attach_shader_program(c)) return 1;
        if (canvas_resize(c, 600, 600, 16)) return 1;
        if (attach_framebuffer_to_canvas(c)) return 1;
        return 0;
}

int
canvas_resize(struct canvas *c, int width, int height, int pt_size)
{
        c->width = width;
        c->height = height;
        c->pt_size = pt_size;

        struct basic_font_info info;
        if (layout_engine_get_basic_font_info(c->r->e, &info, c->pt_size)) {
                fprintf(stderr, "Couldn't get basic information about font\n");
                return 1;
        }

        c->ascender = info.ascender;
        c->cw = info.cw;
        c->ch = info.ch;
        c->col = width / c->cw;
        c->row = height / c->ch;

        glUseProgram(c->program);

        glUniform1i(glGetUniformLocation(c->program, "ascender"), c->ascender);
        glUniform1i(glGetUniformLocation(c->program, "width"), c->width);
        glUniform1i(glGetUniformLocation(c->program, "height"), c->height);
        glUniform1i(glGetUniformLocation(c->program, "cw"), c->cw);
        glUniform1i(glGetUniformLocation(c->program, "ch"), c->ch);
        glUniform1i(glGetUniformLocation(c->program, "col"), c->col);
        glUniform1i(glGetUniformLocation(c->program, "row"), c->row);

        return 0;
}

int
canvas_render_string(struct canvas *c, int x, int y, const char *s)
{
        (void)x, (void)y;

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
        layout(c->r->e, cells, num_cells, glyphs, &num_glyphs, c->pt_size);

        for (unsigned i = 0; i < num_glyphs; i++)
                arr[i] = glyphs[i].index;

        print(LOG_DETAIL, "Uploading %u glyph indices (%d, %d, %d, ...)\n", num_glyphs, arr[0], arr[1], arr[2]);
        printf("%d\n", glGetUniformLocation(c->program, "glyph_indices"));
        glUniform1iv(glGetUniformLocation(c->program, "glyph_indices"), num_glyphs, arr);

        renderer_upload_glyphs(c->r);

        return num_glyphs;
}
