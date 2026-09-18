#version 410 core
#include "ubo.glsl"
layout(location = 0) in vec3 aAnchor;
layout(location = 1) in vec2 aOffset;
layout(location = 2) in vec2 aUv;
layout(location = 3) in vec4 aColor;
layout(location = 4) in float aScreen;
out vec2 vUv;
out vec4 vColor;
void main() {
    vec2 ndcAnchor; float z; float w = 1.0;
    if (aScreen > 0.5) {
        ndcAnchor = vec2(aAnchor.x * uViewport.z * 2.0 - 1.0, 1.0 - aAnchor.y * uViewport.w * 2.0);
        z = -0.999;
    } else {
        vec4 c = uViewProj * vec4(aAnchor, 1.0); // aAnchor is camera-relative (TextBatch subtracts the origin on the CPU)
        if (c.w <= 0.0) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); vUv = aUv; vColor = aColor; return; }
        ndcAnchor = c.xy / c.w;
        z = c.z / c.w;
    }
    vec2 off = vec2(aOffset.x * uViewport.z * 2.0, -aOffset.y * uViewport.w * 2.0);
    gl_Position = vec4(ndcAnchor + off, z, w);
    vUv = aUv;
    vColor = aColor;
}
