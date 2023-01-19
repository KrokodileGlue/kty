#version 330 core

layout (location = 0) in vec2 aPos;

uniform int width;
uniform int height;

uniform int cw;
uniform int ch;

uniform int row;
uniform int col;

uniform vec2 offsets[100];

out vec3 out_color;

void main()
{
        float sx = 2.0 / width;
        float sy = 2.0 / height;
        vec2 offset = vec2((gl_InstanceID % col) * cw * sx - 1.0,
                           1.0 - (gl_InstanceID / col) * ch * sy - ch * sy);
        gl_Position = vec4(vec2(aPos.x * cw * sx, aPos.y * ch * sy) + offset, 0.0, 1.0);
        out_color = vec3(gl_InstanceID / 1000.0 * 1.0,
                         (gl_InstanceID % 3) * 1.0, (gl_InstanceID % 2) * 0.5);
}
