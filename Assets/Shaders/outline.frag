#version 410 core
// Standalone outline pass (used when the post pass is bypassed, e.g. debugging).
#include "ubo.glsl"
in vec2 vUv;
uniform usampler2D uId;
layout(location = 0) out vec4 oColor;
void main() {
    ivec2 px = ivec2(vUv * uViewport.xy);
    uint sel = uSelected.x;
    uint here = texelFetch(uId, px, 0).r;
    bool edge = false;
    for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) {
        uint v = texelFetch(uId, clamp(px + ivec2(dx, dy), ivec2(0), ivec2(uViewport.xy) - 1), 0).r;
        if ((v == sel) != (here == sel)) edge = true;
    }
    oColor = edge && sel != 0u ? uOutlineColor : vec4(0.0);
}
