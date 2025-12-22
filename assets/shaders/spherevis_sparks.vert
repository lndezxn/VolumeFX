#version 430 core

layout(location = 0) in vec2 aQuadPos;
layout(location = 1) in vec4 aPositionLife;
layout(location = 2) in vec4 aVelocityMaxLife;
layout(location = 3) in vec4 aColor;

uniform mat4 uView;
uniform mat4 uProj;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;
uniform float uSize;
uniform float uStreak;

out float vLifeProgress;
out vec3 vColor;

void main() {
    vec3 position = aPositionLife.xyz;
    float life = aPositionLife.w;
    float maxLife = aVelocityMaxLife.w;
    float progress = (maxLife > 0.0) ? (maxLife - life) / maxLife : 0.0;
    vec3 velocity = aVelocityMaxLife.xyz;
    vec3 streakDir = normalize(velocity + vec3(1e-4));
    vec3 shift = uCameraRight * aQuadPos.x * uSize + uCameraUp * (aQuadPos.y - 0.5) * uSize;
    vec3 streak = streakDir * uStreak * aQuadPos.y * uSize;
    vec3 worldPos = position + shift + streak;
    gl_Position = uProj * uView * vec4(worldPos, 1.0);
    vLifeProgress = clamp(progress, 0.0, 1.0);
    vColor = aColor.rgb;
}
