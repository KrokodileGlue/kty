#version 330 core

in vec2 aPos;

uniform int width;
uniform int height;

uniform int cw;
uniform int ch;

uniform int row;
uniform int col;

uniform vec4 glyph_vertex[1000];
uniform vec4 glyph_texcoords[1000];

uniform vec2 offsets[100];

out vec3 out_color;
out vec2 out_tex_coords;

// gl_VertexID

void main()
{
        float sx = 2.0 / width;
        float sy = 2.0 / height;

        vec2 vertex = vec2(0.0);
        vec2 tvertex = vec2(0.0);

        vec2 nw = vec2(glyph_vertex[gl_InstanceID][0], glyph_vertex[gl_InstanceID][1]);
        vec2 se = vec2(glyph_vertex[gl_InstanceID][2], glyph_vertex[gl_InstanceID][3]);

        vec2 tnw = vec2(glyph_texcoords[gl_InstanceID][0], glyph_texcoords[gl_InstanceID][1]);
        vec2 tse = vec2(glyph_texcoords[gl_InstanceID][2], glyph_texcoords[gl_InstanceID][3]);

        int index = gl_VertexID % 6;

        if (index == 0) {
                vertex = nw;
                tvertex = nw;
        } else if (index == 1) {
                vertex = se;
                tvertex = tse;
        } else if (index == 2) {
                vertex = vec2(nw.x, se.y);
                tvertex = vec2(tnw.x, tse.y);
        } else if (index == 3) {
                vertex = nw;
                tvertex = tnw;
        } else if (index == 4) {
                vertex = se;
                tvertex = tse;
        } else if (index == 5) {
                vertex = vec2(se.x, nw.y);
                tvertex = vec2(tse.x, tnw.y);
        }

        vertex = aPos * nw;

        vec2 offset = vec2((gl_InstanceID % col) * cw * sx - 1.0,
                           (gl_InstanceID / col) * -ch * sy);
        gl_Position = vec4(vec2(vertex.x * cw * sx, vertex.y * ch * sy) + offset, 0.0, 1.0);
        out_color = vec3(1.0, (gl_InstanceID % 3) * 1.0, (gl_InstanceID % 2) * 0.5);
        out_tex_coords = tvertex;
}
