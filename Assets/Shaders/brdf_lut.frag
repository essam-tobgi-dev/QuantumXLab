#version 410 core
// Split-sum BRDF integration LUT (spec 18 §4): u = n·v, v = roughness → (scale A, bias B) such
// that ∫ f cosθ dω ≈ F0·A + B. GGX with Schlick–Smith geometry (k = α/2 for IBL).
#include "ibl.glsl"
in vec2 vUv;
layout(location = 0) out vec2 oAB;
const uint N_SAMPLES = 256u;
float gSchlickIbl(float NdotV, float NdotL, float rough) {
    float a = rough * rough;
    float k = a / 2.0;
    float gv = NdotV / (NdotV * (1.0 - k) + k);
    float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}
void main() {
    float NdotV = max(vUv.x, 1e-3);
    float rough = clamp(vUv.y, 0.02, 1.0);
    vec3 V = vec3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);
    vec3 N = vec3(0.0, 0.0, 1.0);
    float A = 0.0, B = 0.0;
    for (uint i = 0u; i < N_SAMPLES; ++i) {
        vec2 xi = hammersley(i, N_SAMPLES);
        vec3 H = importanceSampleGGX(xi, N, rough);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);
        float NdotL = max(L.z, 0.0), NdotH = max(H.z, 0.0), VdotH = max(dot(V, H), 0.0);
        if (NdotL > 0.0) {
            float G = gSchlickIbl(NdotV, NdotL, rough);
            float Gvis = G * VdotH / max(NdotH * NdotV, 1e-4);
            float Fc = pow(1.0 - VdotH, 5.0);
            A += (1.0 - Fc) * Gvis;
            B += Fc * Gvis;
        }
    }
    oAB = vec2(A, B) / float(N_SAMPLES);
}
