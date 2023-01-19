#version 330 core

in vec3 out_color;
in vec2 out_tex_coords;

out vec4 FragColor;

uniform sampler2D spritemap;

void main()
{
        FragColor = texture(spritemap, out_tex_coords);
}