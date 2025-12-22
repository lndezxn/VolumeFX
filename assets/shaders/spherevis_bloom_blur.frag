#version 450 core

layout (location = 0) out vec4 fragColor;

uniform vec2 u_resolution;
uniform sampler2D u_texture;
uniform vec2 u_direction;
uniform float u_blurRadius;

const float weights[9] = float[](0.05, 0.09, 0.12, 0.15, 0.18, 0.15, 0.12, 0.09, 0.05);

void main() {
    vec2 uv = gl_FragCoord.xy / u_resolution;
    vec2 texel = vec2(1.0) / u_resolution;
    vec3 color = texture(u_texture, uv).rgb * weights[4];
    float radius = max(0.01, u_blurRadius);
    for (int i = 1; i <= 4; ++i) {
        vec2 offset = u_direction * texel * (radius * float(i));
        color += texture(u_texture, uv + offset).rgb * weights[4 + i];
        color += texture(u_texture, uv - offset).rgb * weights[4 - i];
    }
    fragColor = vec4(color, 1.0);
}
