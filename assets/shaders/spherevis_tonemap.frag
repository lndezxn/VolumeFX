#version 450 core

layout (location = 0) out vec4 fragColor;

uniform vec2 u_resolution;
uniform sampler2D u_hdrTexture;
uniform sampler2D u_bloomTexture;
uniform float u_exposure;
uniform float u_bloomStrength;
uniform int u_bloomEnabled;
uniform int u_mode;

vec3 ReinhardToneMap(vec3 color) {
    return color / (vec3(1.0) + color);
}

vec3 ACESFilm(vec3 color) {
    const vec3 a = vec3(2.51);
    const vec3 b = vec3(0.03);
    const vec3 c = vec3(2.43);
    const vec3 d = vec3(0.59);
    const vec3 e = vec3(0.14);
    vec3 numerator = color * (a * color + b);
    vec3 denominator = color * (c * color + d) + e;
    return clamp(numerator / denominator, 0.0, 1.0);
}

void main() {
    vec2 uv = gl_FragCoord.xy / u_resolution;
    vec3 hdrColor = texture(u_hdrTexture, uv).rgb;
    vec3 bloomColor = u_bloomEnabled == 1 ? texture(u_bloomTexture, uv).rgb : vec3(0.0);
    vec3 combined = hdrColor + bloomColor * u_bloomStrength;
    combined *= u_exposure;
    vec3 mapped;
    if (u_mode == 1) {
        mapped = ACESFilm(combined);
    } else {
        mapped = ReinhardToneMap(combined);
    }
    mapped = pow(mapped, vec3(1.0 / 2.2));
    fragColor = vec4(mapped, 1.0);
}
