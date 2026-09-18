#version 410 core
// Bloom (spec 18 §5): 9-tap separable Gaussian (σ ≈ 2 texels) evaluated with 5 bilinear fetches
// along uDir. With BRIGHT defined this is the first (horizontal) pass: it reads the full-res
// resolved HDR colour, applies the exposure and the threshold-1.0 (display white) soft knee per
// tap, so LEDs and pulse packets (emissive 4–6) glow and lit white walls (≈ 1) do not.
#include "ubo.glsl"
in vec2 vUv;
uniform sampler2D uColor;
uniform vec2 uDir;       // (1/w, 0) or (0, 1/h) in the TARGET's texel size
layout(location = 0) out vec4 oColor;
const float kW[3] = float[3](0.2270270270, 0.3162162162, 0.0702702703);
const float kO[3] = float[3](0.0, 1.3846153846, 3.2307692308);
vec3 tap(vec2 uv) {
    vec3 c = texture(uColor, uv).rgb;
#ifdef BRIGHT
    c *= uFrameParams.x;   // exposure: the threshold is display-referred white
    float lum = max(max(c.r, c.g), c.b);
    const float threshold = 1.0, knee = 0.5;
    float soft = clamp(lum - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-4);
    c *= max(soft, lum - threshold) / max(lum, 1e-4);
#endif
    return c;
}
void main() {
    vec3 sum = tap(vUv) * kW[0];
    for (int i = 1; i < 3; ++i) {
        sum += tap(vUv + uDir * kO[i]) * kW[i];
        sum += tap(vUv - uDir * kO[i]) * kW[i];
    }
    oColor = vec4(sum, 1.0);
}
