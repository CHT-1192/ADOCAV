#version 460

// Instant tile rendering — per-vertex type mixes per-instance fill/stroke colors
layout(location = 0) in vec3 aPos;
layout(location = 1) in float aType;          // 0.0 = stroke, 1.0 = fill
layout(location = 2) in vec3 aInstOffset;      // camera-relative world offset
layout(location = 3) in vec3 iColor;           // fill color (per-instance)
layout(location = 4) in vec3 iBgColor;         // stroke color (per-instance)
layout(location = 5) in float iOpacity;        // opacity (per-instance)

layout(push_constant) uniform PushConstants {
    mat4 uVP;
} pc;

layout(location = 0) out vec3 vColor;
layout(location = 1) out float vOpacity;

void main() {
    vColor = mix(iBgColor, iColor, aType);
    vOpacity = iOpacity;
    gl_Position = pc.uVP * vec4(aPos + aInstOffset, 1.0);
}
