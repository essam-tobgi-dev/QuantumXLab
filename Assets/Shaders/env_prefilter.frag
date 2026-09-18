#version 410 core
// Specular prefilter (spec 18 §4, split-sum): level k ↔ roughness k/5. GGX importance sampling
// with uSamples Hammersley samples (128 at roughness 0.2, up to 2048 at roughness ≥ 0.6, where
// the lobe spans the hemisphere and the 8²…32² levels are cheap); each sample reads the source
// mip chosen from its pdf so bright panels do not alias into fireflies. Roughness 0 copies the
// source.
#include "ibl.glsl"
in vec3 vDir;
uniform samplerCube uEnv;
uniform float uRoughness;
uniform float uSrcSize;    // source level-0 face size (texels)
uniform uint uSamples;
layout(location = 0) out vec4 oColor;
void main() {
    vec3 N = normalize(vDir);
    if (uRoughness < 1e-3) { oColor = vec4(textureLod(uEnv, N, 0.0).rgb, 1.0); return; }
    vec3 V = N;   // split-sum assumption: view = reflection = normal
    vec3 sum = vec3(0.0);
    float wsum = 0.0;
    float saTexel = 4.0 * IBL_PI / (6.0 * uSrcSize * uSrcSize);
    for (uint i = 0u; i < uSamples; ++i) {
        vec2 xi = hammersley(i, uSamples);
        vec3 H = importanceSampleGGX(xi, N, uRoughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);
        float NdotL = dot(N, L);
        if (NdotL <= 0.0) continue;
        float NdotH = max(dot(N, H), 0.0);
        float pdf = dGGX(NdotH, uRoughness) * NdotH / (4.0 * max(dot(H, V), 1e-4)) + 1e-4;
        float saSample = 1.0 / (float(uSamples) * pdf);
        float mip = clamp(0.5 * log2(saSample / saTexel) + 1.0, 0.0, 7.0);
        sum += textureLod(uEnv, L, mip).rgb * NdotL;
        wsum += NdotL;
    }
    oColor = vec4(sum / max(wsum, 1e-4), 1.0);
}
