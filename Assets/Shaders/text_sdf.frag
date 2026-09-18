#version 410 core
in vec2 vUv;
in vec4 vColor;
uniform sampler2D uAtlas;
layout(location = 0) out vec4 oColor;
layout(location = 1) out uint oId;
void main() {
    float d = texture(uAtlas, vUv).r - 0.5;
    float aa = fwidth(d) * 0.8 + 0.01;
    float alpha = smoothstep(-aa, aa, d);
    oColor = vec4(vColor.rgb, vColor.a * alpha);
    oId = 0u;
}
