#version 330 core

in vec2 out_tex_coords;

out vec4 FragColor;

uniform sampler2D spritemap;

void main()
{
        vec3 color = vec3(1.0, 1.0, 1.0);
        FragColor = vec4(color, texture(spritemap, out_tex_coords));
        //FragColor = vec4(1.0, 1.0, 1.0, 0.5);
}