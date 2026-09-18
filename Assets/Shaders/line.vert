#version 410 core
#include "ubo.glsl"
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aOther;
layout(location = 2) in vec4 aColor;
layout(location = 3) in float aSide;
layout(location = 4) in float aEnd;
layout(location = 5) in float aWidth;
layout(location = 6) in float aDash;
out vec4 vColor;
out float vDist;
out float vDash;
out float vAcross;
out float vHalf;
void main() {
    vec3 rel0 = aPos, rel1 = aOther; // already camera-relative: LineBatch subtracts the origin in double on the CPU
    vec4 c0 = uViewProj * vec4(rel0, 1.0);
    vec4 c1 = uViewProj * vec4(rel1, 1.0);
    // clip both against near plane in a cheap way (drop if behind)
    vec2 s0 = c0.xy / max(c0.w, 1e-5) * uViewport.xy * 0.5;
    vec2 s1 = c1.xy / max(c1.w, 1e-5) * uViewport.xy * 0.5;
    vec2 dir = s1 - s0;
    float len = length(dir);
    dir = len > 1e-4 ? dir / len : vec2(1.0, 0.0);
    vec2 nrm = vec2(-dir.y, dir.x);
    vec4 c = aEnd < 0.5 ? c0 : c1;
    vec2 s = aEnd < 0.5 ? s0 : s1;
    vec2 off = nrm * aSide * (aWidth * 0.5 + 0.75);
    vec2 ndc = (s + off) / (uViewport.xy * 0.5);
    gl_Position = vec4(ndc * c.w, c.z, c.w);
    vColor = aColor;
    vDist = aEnd * len;
    vDash = aDash;
    vAcross = aSide * (aWidth * 0.5 + 0.75);
    vHalf = aWidth * 0.5;
}
