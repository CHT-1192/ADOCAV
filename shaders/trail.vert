#version 460

layout(location = 0) in vec3 aPos;

layout(push_constant) uniform PushConstants {
    mat4 uMVP;
} pc;

void main() {
    gl_Position = pc.uMVP * vec4(aPos, 1.0);
}
