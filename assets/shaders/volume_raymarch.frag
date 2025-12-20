#version 410 core

in vec3 v_WorldPos;

layout(location = 0) out vec4 f_Color;

uniform mat4 u_ModelInv;
uniform vec3 u_CameraPos;
uniform sampler3D u_DensityTex;
uniform vec3 u_BoxMin;
uniform vec3 u_BoxMax;
uniform int u_StepCount;
uniform float u_DensityScale;
uniform float u_Thresh;

float hash12(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

vec2 intersectBox(vec3 rayOrigin, vec3 rayDir, vec3 boxMin, vec3 boxMax) {
    vec3 invDir = 1.0 / rayDir;
    vec3 t0 = (boxMin - rayOrigin) * invDir;
    vec3 t1 = (boxMax - rayOrigin) * invDir;
    vec3 tNear = min(t0, t1);
    vec3 tFar = max(t0, t1);
    float entry = max(max(tNear.x, tNear.y), max(tNear.z, 0.0));
    float exit = min(min(tFar.x, tFar.y), tFar.z);
    return vec2(entry, exit);
}

void main() {
    vec3 rayOrigin = u_CameraPos;
    vec3 rayDir = normalize(v_WorldPos - u_CameraPos);
    vec3 rayOriginLocal = (u_ModelInv * vec4(rayOrigin, 1.0)).xyz;
    vec3 rayDirLocal = normalize(mat3(u_ModelInv) * rayDir);

    vec2 hit = intersectBox(rayOriginLocal, rayDirLocal, u_BoxMin, u_BoxMax);
    float tEntry = max(hit.x, 0.0);
    float tExit = hit.y;
    if (tExit <= tEntry) {
        discard;
    }

    int steps = max(u_StepCount, 1);
    float dt = (tExit - tEntry) / float(steps);
    if (dt <= 0.0) {
        discard;
    }

    vec3 accumColor = vec3(0.0);
    float accumAlpha = 0.0;
    vec3 boxExtent = u_BoxMax - u_BoxMin;
    vec3 baseColor = vec3(1.0);

    float jitter = (hash12(gl_FragCoord.xy) - 0.5) * dt;

    for (int i = 0; i < steps && accumAlpha < 0.98; ++i) {
        float t = tEntry + jitter + (float(i) + 0.5) * dt;
        vec3 samplePos = rayOriginLocal + rayDirLocal * t;
        vec3 texCoord = (samplePos - u_BoxMin) / boxExtent;
        if (any(lessThan(texCoord, vec3(0.0))) || any(greaterThan(texCoord, vec3(1.0)))) {
            continue;
        }

        float density = texture(u_DensityTex, texCoord).r;
        if (density < u_Thresh) {
            continue;
        }

        float sigma = density * u_DensityScale;
        float absorption = 1.0 - exp(-sigma * dt);
        float weight = (1.0 - accumAlpha) * absorption;
        accumColor += weight * baseColor;
        accumAlpha += weight;
    }

    if (accumAlpha <= 0.0) {
        discard;
    }

    f_Color = vec4(accumColor, accumAlpha);
}
