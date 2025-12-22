#version 430 core

layout(location = 0) out vec4 outColor;

uniform float u_time;
uniform vec2 u_resolution;
uniform float u_audioBass;
uniform float u_audioTreble;
uniform int u_mode;
uniform float u_intensity;
uniform float u_speed;
uniform vec3 u_colorA;
uniform vec3 u_colorB;

float Hash(vec2 value) {
    value = fract(value * vec2(127.1, 311.7));
    value += dot(value, value + 19.19);
    return fract(sin(dot(value, vec2(12.9898, 78.233))) * 43758.5453123);
}

float Noise(vec2 point) {
    vec2 floorPoint = floor(point);
    vec2 fracPoint = fract(point);
    float a = Hash(floorPoint);
    float b = Hash(floorPoint + vec2(1.0, 0.0));
    float c = Hash(floorPoint + vec2(0.0, 1.0));
    float d = Hash(floorPoint + vec2(1.0, 1.0));
    vec2 fade = fracPoint * fracPoint * (3.0 - 2.0 * fracPoint);
    return mix(mix(a, b, fade.x), mix(c, d, fade.x), fade.y);
}

float fbm(vec2 point, float time) {
    float total = 0.0;
    float amplitude = 0.7;
    float frequency = 1.0;
    for (int i = 0; i < 5; ++i) {
        total += amplitude * Noise(point * frequency + vec2(time * 0.1));
        frequency *= 2.0;
        amplitude *= 0.5;
    }
    return total;
}

vec3 GradientLayer(vec2 uv, float time) {
    float t = smoothstep(0.0, 1.0, uv.y);
    vec3 base = mix(u_colorA, u_colorB, pow(t, 1.2));
    float detail = (Noise(uv * 320.0 + vec2(time * 0.05, -time * 0.04)) - 0.5) * 0.12 * (1.0 + u_audioTreble * 0.6);
    base += detail;
    return clamp(base, 0.0, 1.0);
}

float StarField(vec2 uv, float time) {
    // Irregular star distribution with varied sizes (few large).
    vec2 grid = uv * vec2(50.0);
    vec2 cell = floor(grid);
    vec2 local = fract(grid);

    float r0 = Hash(cell);
    float r1 = Hash(cell + vec2(7.13, 3.11));
    float r2 = Hash(cell + vec2(5.17, 9.29));

    // Jitter position to break lattice patterns.
    vec2 jitter = vec2(r1, r2) - 0.5;
    vec2 starPos = vec2(0.5) + jitter * 0.6;
    float dist = length(local - starPos);

    // Sparse spawning: more random, fewer stars overall.
    float density = 0.35 + 0.25 * u_audioBass; // base spawn rate
    float spawn = step(r0, density * pow(r1, 1.4)); // favor randomness

    // Size: heavy-tailed so a few are big.
    float bigBias = (r2 < 0.08) ? 0.14 : 0.0; // rare big stars
    float radius = mix(0.01, 0.16 + bigBias, pow(r0, 0.35)) * (0.7 + u_audioTreble * 0.6);
    float glow = smoothstep(radius, 0.0, dist);

    float twinkle = 0.45 + 0.55 * sin(time * (0.9 + r0 * 5.5) + r2 * 6.2831);
    float peak = glow * spawn * twinkle;

    // Soft background scatter to avoid perfect emptiness.
    float scatter = Noise(uv * 10.0 + vec2(time * 0.07)) * 0.08;
    return clamp(peak * (1.7 + u_audioBass * 1.1) + scatter, 0.0, 3.0);
}

vec3 NebulaLayer(vec2 uv, float time) {
    vec2 p = uv * (1.3 + u_audioBass * 0.6);
    float swirl = fbm(p + vec2(time * 0.12), time);
    p += vec2(cos(swirl * 6.2831), sin(swirl * 6.2831)) * (0.25 + u_audioBass * 0.2);
    float density = fbm(p * (1.0 + u_audioTreble), time) * 0.8;
    density = clamp(density, 0.0, 1.0);
    vec3 color = mix(u_colorA, u_colorB, 0.5 + 0.5 * density);
    color += vec3(density * 0.6 + u_audioTreble * 0.4);
    return clamp(color, 0.0, 1.0);
}

void main() {
    vec2 safeRes = max(u_resolution, vec2(1.0));
    vec2 uv = gl_FragCoord.xy / safeRes;
    vec2 centered = uv - 0.5;
    centered.x *= safeRes.x / safeRes.y;
    float time = u_time * max(0.1, u_speed);
    vec3 gradient = GradientLayer(uv, time);
    float stars = StarField(centered, time);
    vec3 starColor = mix(u_colorA, u_colorB, 0.5) * stars * 1.4;
    vec3 nebula = NebulaLayer(centered, time);
    vec3 color = gradient;
    if (u_mode == 1) {
        // Starfield: dark base, bright sparse stars.
        vec3 base = vec3(0.015);
        color = base + starColor * 1.9;
    } else if (u_mode == 2) {
        float blend = clamp((nebula.r + nebula.g + nebula.b) / 3.0, 0.0, 1.0);
        color = mix(color, nebula, blend * 0.9 + 0.1);
    }
    color *= clamp(u_intensity, 0.0, 2.0);
    outColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
