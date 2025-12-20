#version 410 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Color;

uniform mat4 u_Model;
uniform mat4 u_ViewProj;

out vec3 v_Color;

void main() {
    v_Color = a_Color;
    gl_Position = u_ViewProj * u_Model * vec4(a_Position, 1.0);
}
