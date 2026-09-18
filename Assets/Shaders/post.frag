#version 410 core
// Post pass (spec 18 §5 passes 8–10): SSAO multiply, bloom add, selection outline, ACES tonemap.
#include "ubo.glsl"
in vec2 vUv;
uniform sampler2D uColor;
uniform usampler2D uId;
uniform sampler2D uAo;      // R8 full resolution (1 = open)
uniform sampler2D uBloom;   // half-resolution blurred bright pass
layout(location = 0) out vec4 oColor;
vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
void main() {
    vec3 hdr = texture(uColor, vUv).rgb;
    // Screen-space AO on the whole resolved colour: darkens direct light too, an accepted
    // approximation for GL 4.1 forward rendering (spec 18 §5 pass 8).
    hdr *= mix(1.0, texture(uAo, vUv).r, uPostParams.z);
    hdr *= uFrameParams.x;   // exposure
    hdr += texture(uBloom, vUv).rgb * uPostParams.w;   // bloom is already exposed (bloom_blur BRIGHT)
    vec3 c = pow(aces(hdr), vec3(1.0 / 2.2));
    // selection outline: id-buffer edge detection (spec 18 §4/§5 pass 9). A pixel is outline when
    // any of 16 neighbours within the outline width w lies on the other side of the selection
    // boundary. Four textureGather calls centred on (±w, ±½)/(±½, ±w) fetch the 2×2 blocks
    // {(w,0),(w−1,0),(w,1),(w−1,1)} etc.: the four axis points at radius w plus near-diagonals,
    // in 5 fetches instead of the (2w+1)² = 25 of a full square.
    uint sel = uSelected.x;
    if (sel != 0u) {
        ivec2 px = ivec2(vUv * uViewport.xy);
        float w = uSelParams.x;
        uint here = texelFetch(uId, px, 0).r;
        bool inside = here == sel;
        vec2 c0 = vec2(px) + 0.5;
        vec2 g[4] = vec2[4](vec2(w, 0.5), vec2(-w, -0.5), vec2(0.5, -w), vec2(-0.5, w));
        bool outside = false;
        for (int i = 0; i < 4 && !outside; ++i) {
            uvec4 ids = textureGather(uId, (c0 + g[i]) * uViewport.zw, 0);
            bvec4 same = equal(ids, uvec4(sel));
            if (inside ? !all(same) : any(same)) outside = true;
        }
        if (outside) c = mix(c, uOutlineColor.rgb, 0.9);
    }
    oColor = vec4(c, 1.0);
}
