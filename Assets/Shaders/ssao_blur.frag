#version 410 core
// Separable bilateral blur of the SSAO term (spec 18 §5 pass 8): 5 taps (σ = 1 source texel)
// weighted by depth similarity so occlusion never bleeds across silhouettes.
//   HORIZONTAL: half-res (ao, depth) → half-res (ao, depth).
//   VERTICAL (define UPSAMPLE): half-res (ao, depth) → full-res R8; the centre depth is the
//   full-resolution linear depth, so this pass is also the depth-aware upsample.
#include "ubo.glsl"
in vec2 vUv;
uniform sampler2D uAo;       // half-res (ao, linear depth)
uniform vec2 uDir;           // step in uAo's uv units: (1/w, 0) or (0, 1/h)
#ifdef UPSAMPLE
uniform sampler2D uDepth;    // full-res linear depth
layout(location = 0) out float oAo;
#else
layout(location = 0) out vec2 oAo;
#endif
const float kW[3] = float[3](0.38774, 0.24477, 0.06136);
void main() {
#ifdef UPSAMPLE
    float zc = texelFetch(uDepth, ivec2(gl_FragCoord.xy), 0).r;
#else
    float zc = texture(uAo, vUv).g;
#endif
    if (zc <= 0.0) {
#ifdef UPSAMPLE
        oAo = 1.0;
#else
        oAo = vec2(1.0, 0.0);
#endif
        return;
    }
    float sum = 0.0, wsum = 0.0;
    for (int i = -2; i <= 2; ++i) {
        vec2 s = texture(uAo, vUv + uDir * float(i)).rg;
        float w = kW[abs(i)] * exp(-abs(s.g - zc) / max(zc * 0.02, 1e-6));
        sum += s.r * w;
        wsum += w;
    }
    float ao = wsum > 1e-5 ? sum / wsum : 1.0;
#ifdef UPSAMPLE
    oAo = ao;
#else
    oAo = vec2(ao, zc);
#endif
}
