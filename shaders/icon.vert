#version 460

// Icon rendering — identical to tile.vert but with visibility culling
layout(location = 0) in vec3 aPos;
layout(location = 1) in float aType;
layout(location = 2) in vec3 aInstOffset;
layout(location = 3) in vec3 iColor;
layout(location = 4) in vec3 iBgColor;
layout(location = 5) in float iOpacity;
layout(location = 6) in uint aVisible;          // 1=visible, 0=GPU-culled

layout(push_constant) uniform PushConstants {
    mat4 uVP;
} pc;

layout(location = 0) out vec3 vColor;
layout(location = 1) out float vOpacity;

void main() {
    if (aVisible == 0u) {
        gl_Position = vec4(0.0 / 0.0);
        vColor = vec3(0.0);
        vOpacity = 0.0;
        return;
    }
    vColor = mix(iBgColor, iColor, aType);
    vOpacity = iOpacity;
    gl_Position = pc.uVP * vec4(aPos + aInstOffset, 1.0);
}
