#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec2 aUv;
out vec2 vUv;
void main() { vUv = aUv; gl_Position = vec4(aPos.xy, 0.0, 1.0); }
