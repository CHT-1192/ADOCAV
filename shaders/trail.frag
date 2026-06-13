#version 460

layout(push_constant) uniform PushConstants {
    layout(offset = 64) vec4 uColor;
} pc;

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = pc.uColor;
}
