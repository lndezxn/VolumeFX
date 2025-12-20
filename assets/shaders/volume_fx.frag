#version 410 core

in vec3 v_Color;

out vec4 f_Color;

void main() {
    f_Color = vec4(v_Color, 1.0);
}
