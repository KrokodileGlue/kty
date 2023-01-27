#version 330 core

in vec2 aPos;

uniform int ascender;

uniform int width;
uniform int height;

uniform int cw;
uniform int ch;

uniform int row;
uniform int col;

uniform ivec2 glyph_position[1000];
uniform vec2 glyph_vertex[1000];
uniform vec4 glyph_texcoords[1000];

uniform int glyph_indices[4096];

out vec3 out_color;
out vec2 out_tex_coords;

// gl_VertexID

void main()
{
        float sx = 2.0 / width;
        float sy = 2.0 / height;

        vec2 tvertex = vec2(0.0);

        int glyph_index = glyph_indices[gl_InstanceID];

        vec2 tnw = vec2(glyph_texcoords[glyph_index][0], glyph_texcoords[glyph_index][1]);
        vec2 tse = vec2(glyph_texcoords[glyph_index][2], glyph_texcoords[glyph_index][3]);

        int index = gl_VertexID % 6;

        if (index == 0) {
                tvertex = tnw;
        } else if (index == 1) {
                tvertex = tse;
        } else if (index == 2) {
                tvertex = vec2(tnw.x, tse.y);
        } else if (index == 3) {
                tvertex = tnw;
        } else if (index == 4) {
                tvertex = tse;
        } else if (index == 5) {
                tvertex = vec2(tse.x, tnw.y);
        }

        vec2 vertex = aPos * glyph_vertex[glyph_index];

        vec2 offset = vec2((gl_InstanceID % col) * cw * sx - 1.0,
                           (gl_InstanceID / col + 1) * -ch * sy + 1.0);
        offset += vec2(sx, sy) * glyph_position[glyph_index];
        offset.y += sy * -ascender;
        gl_Position = vec4(vec2(vertex.x * cw * sx, vertex.y * ch * sy) + offset, 0.0, 1.0);
        out_tex_coords = tvertex;
}
