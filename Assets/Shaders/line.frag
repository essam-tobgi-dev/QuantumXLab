#version 410 core
in vec4 vColor;
in float vDist;
in float vDash;
in float vAcross;   // signed distance from the centre line in px (|vAcross| = halfWidth + 0.75 at the edge)
in float vHalf;
layout(location = 0) out vec4 oColor;
layout(location = 1) out uint oId;
void main() {
    float d = abs(vAcross);
    float alpha = 1.0 - smoothstep(vHalf - 0.5, vHalf + 0.75, d);
    if (vDash > 0.0 && mod(vDist, vDash * 2.0) > vDash) discard;
    oColor = vec4(vColor.rgb, vColor.a * alpha);
    oId = 0u;
}
