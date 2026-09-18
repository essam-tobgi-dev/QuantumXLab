#version 410 core
#include "ubo.glsl"
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in vec4 aColor;
#ifdef INSTANCED
layout(location = 4) in mat4 aInstModel;   // 4..7
layout(location = 8) in uvec2 aInstId;
layout(location = 9) in vec4 aInstColor;
#endif
out vec3 vWorld;
out vec3 vNormal;
out vec2 vUv;
out vec4 vColor;
out vec4 vShadow;
flat out uint vId;
void main() {
#ifdef INSTANCED
    mat4 model = uModel * aInstModel;
    mat3 nmat = transpose(inverse(mat3(aInstModel)));
    vColor = aColor * aInstColor;
    vId = aInstId.x;
#else
    mat4 model = uModel;
    mat3 nmat = mat3(uNormal);
    vColor = aColor;
    vId = uObjectId.x;
#endif
    vec4 w = model * vec4(aPos, 1.0);
    vWorld = w.xyz;
    vNormal = normalize(nmat * aNormal);
    vUv = aUv;
    vShadow = uLightViewProj * w;
    gl_Position = uViewProj * w;
}
