#version 430 core

in float vLifeProgress;
in vec3 vColor;
out vec4 FragColor;

void main() {
    float alpha = clamp(1.0 - vLifeProgress, 0.0, 1.0);
    float intensity = alpha * alpha;
    FragColor = vec4(vColor * intensity, alpha);
}
