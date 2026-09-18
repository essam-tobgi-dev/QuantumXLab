#version 410 core
// Picking id pass (single-sample R32UI target; spec 18 §4, macOS integer-MSAA limitation).
// Attachment 1 carries the linear view depth (−z_view, metres) that SSAO consumes (spec 18 §5
// pass 8), so unpickable geometry is drawn too, with uObjectId.y bit1 set → id 0.
#include "ubo.glsl"
flat in uint vId;
in vec3 vWorld; in vec3 vNormal; in vec2 vUv; in vec4 vColor; in vec4 vShadow;
layout(location = 0) out uint oId;
layout(location = 1) out float oDepth;
void main() {
    oId = (uObjectId.y & 2u) != 0u ? 0u : vId;
    oDepth = -(uView * vec4(vWorld, 1.0)).z;
}
