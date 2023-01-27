#version 330 core

in vec2 out_tex_coords;
flat in int tex;
flat in int is_color;

out vec4 FragColor;

uniform sampler2DArray color_textures;
uniform sampler2DArray alpha_textures;

void main()
{
        vec3 color = vec3(1.0, 1.0, 1.0);

        if (is_color == 1) {
                FragColor = texture(color_textures, vec3(out_tex_coords, tex)).bgra;
        } else {
                FragColor = vec4(color, texture(alpha_textures, vec3(out_tex_coords, tex)));
        }
        //FragColor = vec4(1.0, 1.0, 1.0, 0.5);
}