#version 450 core

layout (location = 0) out vec4 fragColor;

uniform vec2 u_resolution;
uniform sampler2D u_hdrTexture;
uniform float u_threshold;
uniform float u_knee;

void main() {
    vec2 uv = gl_FragCoord.xy / u_resolution;
    vec3 color = texture(u_hdrTexture, uv).rgb;
    float brightness = max(max(color.r, color.g), color.b);
    float knee = max(0.0, u_knee);
    float soft = 0.0;
    if (knee > 0.0) {
        float range = knee * 2.0 + 1e-5;
        soft = clamp((brightness - u_threshold + knee) / range, 0.0, 1.0);
    } else {
        soft = brightness > u_threshold ? 1.0 : 0.0;
    }
    vec3 bloom = max(color - vec3(u_threshold), vec3(0.0));
    bloom *= soft;
    fragColor = vec4(bloom, 1.0);
}
