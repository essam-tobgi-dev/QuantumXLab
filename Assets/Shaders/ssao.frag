#version 410 core
// Screen-space ambient occlusion (spec 18 §5 pass 8), computed at HALF resolution from the linear
// view depth written by the id pass (same scene, same frame). The normal comes from depth
// derivatives (the smaller one-sided difference per axis), the kernel is a 16-sample hemisphere
// with a 4×4 per-pixel rotation, and the sampling radius is clamp(0.05 · depth, 1 mm, 0.5 m) so
// both the chip and the room work. Output: (occlusion, linear depth) for the bilateral blur.
#include "ubo.glsl"
in vec2 vUv;
uniform sampler2D uDepth;   // full-resolution linear depth (R32F, 0 = background)
layout(location = 0) out vec2 oAo;

const int KERNEL = 16;
// Hemisphere kernel: every z ≥ 0.12 so a flat surface never occludes itself.
const vec3 kKernel[16] = vec3[16](
    vec3( 0.5381, 0.1856, 0.4319), vec3( 0.1379, 0.2486, 0.4430), vec3( 0.3371, 0.5679, 0.1257),
    vec3(-0.6999,-0.0451, 0.1519), vec3( 0.0689,-0.1598, 0.8547), vec3( 0.0560, 0.0069, 0.1843),
    vec3(-0.0146, 0.1402, 0.2762), vec3( 0.0100,-0.1924, 0.1344), vec3(-0.3577,-0.5301, 0.4358),
    vec3(-0.3169, 0.1063, 0.2158), vec3( 0.0103,-0.5869, 0.1546), vec3(-0.0897,-0.4940, 0.3287),
    vec3( 0.7119,-0.0154, 0.1918), vec3(-0.0533, 0.0596, 0.5411), vec3( 0.0352,-0.0631, 0.5460),
    vec3(-0.4776, 0.2847, 0.2271));

float depthAt(ivec2 px) { return texelFetch(uDepth, clamp(px, ivec2(0), textureSize(uDepth, 0) - 1), 0).r; }
// View-space position of full-resolution pixel px with linear depth d (perspective or ortho).
vec3 viewPos(ivec2 px, float d) {
    vec2 ndc = (vec2(px) + 0.5) * uViewport.zw * 2.0 - 1.0;
    float wc = uFrameParams.w > 0.5 ? 1.0 : d;
    return vec3((ndc.x * wc - uProj[3][0]) / uProj[0][0], (ndc.y * wc - uProj[3][1]) / uProj[1][1], -d);
}

void main() {
    ivec2 px = ivec2(gl_FragCoord.xy) * 2;      // full-resolution pixel of this half-res texel
    float d = depthAt(px);
    if (d <= 0.0) { oAo = vec2(1.0, 0.0); return; }   // background
    vec3 P = viewPos(px, d);
    vec3 Pr = viewPos(px + ivec2(1, 0), depthAt(px + ivec2(1, 0))), Pl = viewPos(px - ivec2(1, 0), depthAt(px - ivec2(1, 0)));
    vec3 Pu = viewPos(px + ivec2(0, 1), depthAt(px + ivec2(0, 1))), Pd = viewPos(px - ivec2(0, 1), depthAt(px - ivec2(0, 1)));
    vec3 dx = length(Pr - P) < length(P - Pl) ? Pr - P : P - Pl;
    vec3 dy = length(Pu - P) < length(P - Pd) ? Pu - P : P - Pd;
    vec3 N = normalize(cross(dx, dy));
    float radius = clamp(0.05 * d, 1e-3, 0.5);
    ivec2 ip = ivec2(gl_FragCoord.xy) & 3;      // 4×4 interleaved rotation
    float ang = float(ip.x * 4 + ip.y) * (6.28318530718 / 16.0) + 0.3;
    vec3 rnd = vec3(cos(ang), sin(ang), 0.0);
    vec3 T = normalize(rnd - N * dot(rnd, N));
    vec3 B = cross(N, T);
    mat3 tbn = mat3(T, B, N);
    ivec2 full = textureSize(uDepth, 0);
    float occ = 0.0;
    for (int i = 0; i < KERNEL; ++i) {
        float scale = mix(0.1, 1.0, float(i) / float(KERNEL));
        vec3 sp = P + tbn * kKernel[i] * radius * scale;
        vec4 c = uProj * vec4(sp, 1.0);
        vec2 suv = c.xy / c.w * 0.5 + 0.5;
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) continue;
        float sd = depthAt(ivec2(suv * vec2(full)));
        if (sd <= 0.0) continue;                 // sample lands on the background: unoccluded
        float sz = -sd;                          // scene surface z at the sample's pixel
        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(P.z - sz));
        occ += (sz >= sp.z + 0.02 * radius ? 1.0 : 0.0) * rangeCheck;
    }
    oAo = vec2(1.0 - occ / float(KERNEL), d);
}
