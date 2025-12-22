#version 430 core

in float vLifeProgress;
in vec3 vColor;
in vec2 vQuadPos;
in vec2 vMotionDir;
out vec4 FragColor;

uniform int uMotionBlurEnabled;
uniform float uMotionBlurStrength;

void main() {
    float alpha = clamp(1.0 - vLifeProgress, 0.0, 1.0);
    if (uMotionBlurEnabled != 0) {
        vec2 dir = normalize(vMotionDir + vec2(1e-4));
        vec2 tangent = vec2(-dir.y, dir.x);
        float motionAmount = clamp(length(vMotionDir) * 180.0, 0.0, 4.0);
        float stretch = 1.0 + uMotionBlurStrength * motionAmount;
        vec2 local = vec2(vQuadPos.x, vQuadPos.y - 0.25);
        float along = dot(local, dir);
        float across = dot(local, tangent);
        float alongMask = smoothstep(-0.5 * stretch, stretch, along + 0.1);
        float acrossMask = exp(-abs(across) * (3.0 / (stretch + 1.0)));
        float motionMask = clamp(alongMask * acrossMask, 0.0, 1.0);
        alpha *= motionMask;
    }
    float intensity = alpha * alpha;
    FragColor = vec4(vColor * intensity, alpha);
}
