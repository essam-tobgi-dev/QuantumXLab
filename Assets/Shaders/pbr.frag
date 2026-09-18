#version 410 core
#include "ubo.glsl"
in vec3 vWorld;
in vec3 vNormal;
in vec2 vUv;
in vec4 vColor;
in vec4 vShadow;
flat in uint vId;
layout(location = 0) out vec4 oColor;
layout(location = 1) out uint oId;
uniform sampler2DShadow uShadowMap;
uniform sampler1D uColormap;
uniform samplerCube uIrradiance;    // E(N)/π, 32²
uniform samplerCube uPrefiltered;   // GGX-prefiltered radiance, level k ↔ roughness k/5
uniform sampler2D uBrdfLut;         // (A, B) over (n·v, roughness)

const float PI = 3.14159265359;
const float PREFILTER_LEVELS = 5.0;
// Cook–Torrance GGX (spec 18 §3)
float D_GGX(float NdotH, float a) { float a2 = a * a; float d = NdotH * NdotH * (a2 - 1.0) + 1.0; return a2 / (PI * d * d); }
float G_Smith(float NdotV, float NdotL, float rough) {
    float k = (rough + 1.0) * (rough + 1.0) / 8.0;
    float gv = NdotV / (NdotV * (1.0 - k) + k);
    float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}
vec3 F_Schlick(float cosT, vec3 F0) { return F0 + (1.0 - F0) * pow(1.0 - cosT, 5.0); }
vec3 F_SchlickRough(float cosT, vec3 F0, float rough) {
    return F0 + (max(vec3(1.0 - rough), F0) - F0) * pow(1.0 - cosT, 5.0);
}

// Soft shadow (spec 18 §4 amended): 16-tap Poisson disc of radius uShadowParams.y texels,
// rotated per pixel by interleaved-gradient noise; the lookup position is offset along the
// normal by uShadowParams.x metres (1.5 texels of world size) — no acne at grazing angles
// without peter-panning on the chip.
const vec2 kPoisson[16] = vec2[16](
    vec2(-0.94201624, -0.39906216), vec2( 0.94558609, -0.76890725), vec2(-0.09418410, -0.92938870),
    vec2( 0.34495938,  0.29387760), vec2(-0.91588581,  0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543,  0.27676845), vec2( 0.97484398,  0.75648379), vec2( 0.44323325, -0.97511554),
    vec2( 0.53742981, -0.47373420), vec2(-0.26496911, -0.41893023), vec2( 0.79197514,  0.19090188),
    vec2(-0.24188840,  0.99706507), vec2(-0.81409955,  0.91437590), vec2( 0.19984126,  0.78641367),
    vec2( 0.14383161, -0.14100790));
float interleavedGradientNoise(vec2 px) { return fract(52.9829189 * fract(dot(px, vec2(0.06711056, 0.00583715)))); }

float shadowFactor(vec3 N) {
    if ((uObjectId.y & 1u) == 0u) return 1.0;
    vec4 sp = uLightViewProj * vec4(vWorld + N * uShadowParams.x, 1.0);
    vec3 p = sp.xyz / sp.w * 0.5 + 0.5;
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) return 1.0;
    float bias = uFrameParams.y;
    float t = uFrameParams.z * uShadowParams.y;
    float ang = interleavedGradientNoise(gl_FragCoord.xy) * 2.0 * PI;
    float c = cos(ang), s = sin(ang);
    mat2 rot = mat2(c, s, -s, c);
    float sum = 0.0;
    for (int i = 0; i < 16; ++i)
        sum += texture(uShadowMap, vec3(p.xy + rot * kPoisson[i] * t, p.z - bias));
    return sum / 16.0;
}

vec3 brdf(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, float metallic, float rough) {
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = F_Schlick(max(dot(H, V), 0.0), F0);
    float D = D_GGX(NdotH, rough * rough);
    float G = G_Smith(NdotV, NdotL, rough);
    vec3 spec = D * G * F / (4.0 * NdotV * NdotL + 1e-4);
    vec3 kd = (1.0 - F) * (1.0 - metallic);
    return (kd * albedo / PI + spec) * radiance * NdotL;
}

// Split-sum image-based ambient (spec 18 §4 amended):
//   kd·albedo·irradiance(N) + prefiltered(R, rough)·(F0·A + B), times the material ao.
vec3 ambientIbl(vec3 N, vec3 V, vec3 albedo, float metallic, float rough, float ao) {
    float NdotV = max(dot(N, V), 1e-4);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = F_SchlickRough(NdotV, F0, rough);
    vec3 kd = (1.0 - F) * (1.0 - metallic);
    vec3 irr = texture(uIrradiance, N).rgb;
    vec3 R = reflect(-V, N);
    vec3 pre = textureLod(uPrefiltered, R, rough * PREFILTER_LEVELS).rgb;
    vec2 ab = texture(uBrdfLut, vec2(NdotV, rough)).rg;
    return (kd * albedo * irr + pre * (F0 * ab.x + ab.y)) * ao * uPostParams.x;
}

void main() {
    vec4 base = uBaseColor * vColor;
    if (uProps.w > 0.0) base.rgb = mix(base.rgb, texture(uColormap, uMisc.x).rgb, uProps.w);
    vec3 albedo = base.rgb;
    float metallic = uProps.x, rough = clamp(uProps.y, 0.04, 1.0), ao = uProps.z;
    vec3 N = normalize(vNormal);
    vec3 V = normalize(-vWorld); // camera at origin (camera-relative)
    if (!gl_FrontFacing) N = -N;
    vec3 color;
    if (uMisc.y > 0.5) {
        color = albedo;
    } else {
        vec3 Lsun = normalize(-uSun.direction.xyz);
        color = brdf(N, V, Lsun, uSun.color.rgb * uSun.color.w, albedo, metallic, rough) * shadowFactor(N);
        int n = int(uAmbient.w);
        for (int i = 0; i < n; ++i) {
            vec3 d = uPoints[i].position.xyz - vWorld;
            float dist = length(d);
            float r = max(uPoints[i].position.w, 1e-3);
            float att = 1.0 / (1.0 + (dist / r) * (dist / r));
            color += brdf(N, V, d / max(dist, 1e-6), uPoints[i].color.rgb * uPoints[i].color.w * att, albedo, metallic, rough);
        }
        if (uPostParams.y > 0.5) {
            color += ambientIbl(N, V, albedo, metallic, rough, ao);
        } else {
            // IBL off: the legacy hemispherical constant ambient with fresnel-tinted specular
            vec3 amb = uAmbient.rgb * ao;
            vec3 F0 = mix(vec3(0.04), albedo, metallic);
            float hemi = 0.5 + 0.5 * N.y;
            color += amb * hemi * mix(albedo, F0, metallic * 0.5);
        }
    }
    color += uEmissive.rgb * uEmissive.w;
    if (vId != 0u && vId == uSelected.y) color = mix(color, uHoverColor.rgb, 0.25);
    oColor = vec4(color, base.a);
    oId = vId;
}
