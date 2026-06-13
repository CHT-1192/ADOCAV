#version 460

// Icon rendering — identical to tile.vert (per-vertex type mixes per-instance colors)
layout(location = 0) in vec3 aPos;
layout(location = 1) in float aType;          // 1.0 = fill (icons are fill-only)
layout(location = 2) in vec3 aInstOffset;      // camera-relative world offset
layout(location = 3) in vec3 iColor;           // icon color (per-instance)
layout(location = 4) in vec3 iBgColor;         // icon color (per-instance, same as iColor)
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
