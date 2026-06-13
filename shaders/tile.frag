#version 460

layout(location = 0) in vec3 vColor;
layout(location = 1) in float vOpacity;

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = vec4(vColor, vOpacity);
}
