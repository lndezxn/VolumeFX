#version 430 core

layout(location = 0) out vec4 outColor;

uniform mat4 uInvViewProj;
uniform vec3 uCameraPos;
uniform vec2 uScreenSize;
uniform float uStepSize;
uniform int uMaxSteps;
uniform float uAlphaScale;
uniform int uColorMode;
uniform int uEnableJitter;
uniform float uJitterSeed;
uniform vec3 uVolumeMin;
uniform vec3 uVolumeMax;
layout(binding = 0) uniform sampler3D uVolumeTexture;

layout(std430, binding = 0) buffer RayMarchStats {
    uint totalSteps;
    uint rayCount;
    uint earlyExitCount;
};

float Hash(vec2 value) {
    return fract(sin(dot(value, vec2(12.9898, 78.233))) * 43758.5453);
}

float RandomJitter(vec2 coord, float seed) {
    return Hash(coord + seed);
}

void main() {
    vec2 ndc = (gl_FragCoord.xy / uScreenSize) * 2.0 - 1.0;
    vec4 clip = vec4(ndc, -1.0, 1.0);
    vec4 world = uInvViewProj * clip;
    world /= world.w;
    vec3 rayDir = normalize(world.xyz - uCameraPos);

    vec3 invDir = 1.0 / rayDir;
    vec3 t0 = (uVolumeMin - uCameraPos) * invDir;
    vec3 t1 = (uVolumeMax - uCameraPos) * invDir;
    vec3 tMin = min(t0, t1);
    vec3 tMax = max(t0, t1);

    float entry = max(max(tMin.x, tMin.y), tMin.z);
    float exit = min(min(tMax.x, tMax.y), tMax.z);

    atomicAdd(rayCount, 1u);
    if (exit <= max(entry, 0.0)) {
        outColor = vec4(0.0);
        return;
    }

    float t = max(entry, 0.0);
    if (uEnableJitter == 1) {
        t += (RandomJitter(gl_FragCoord.xy, uJitterSeed) - 0.5) * uStepSize;
    }

    vec3 accumColor = vec3(0.0);
    float accumAlpha = 0.0;
    bool earlyExit = false;
    int steps = 0;

    for (int i = 0; i < uMaxSteps; ++i) {
        float sampleT = t + float(i) * uStepSize;
        if (sampleT > exit) {
            break;
        }

        vec3 samplePos = uCameraPos + rayDir * sampleT;
        vec3 texCoord = (samplePos - uVolumeMin) / (uVolumeMax - uVolumeMin);
        if (any(lessThan(texCoord, vec3(0.0))) || any(greaterThan(texCoord, vec3(1.0)))) {
            continue;
        }

        float density = texture(uVolumeTexture, texCoord).r;
        float alpha = clamp(density * uAlphaScale, 0.0, 1.0);
        vec3 color = (uColorMode == 0)
            ? vec3(density)
            : mix(vec3(0.15, 0.3, 0.7), vec3(1.0, 0.7, 0.2), density);

        accumColor += (1.0 - accumAlpha) * color * alpha;
        accumAlpha += (1.0 - accumAlpha) * alpha;
        steps++;

        if (accumAlpha >= 0.98) {
            earlyExit = true;
            break;
        }
    }

    atomicAdd(totalSteps, uint(steps));
    if (earlyExit) {
        atomicAdd(earlyExitCount, 1u);
    }

    outColor = vec4(accumColor, accumAlpha);
}
