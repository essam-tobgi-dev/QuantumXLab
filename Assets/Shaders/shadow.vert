#version 410 core
#include "ubo.glsl"
layout(location = 0) in vec3 aPos;
#ifdef INSTANCED
layout(location = 4) in mat4 aInstModel;
#endif
void main() {
#ifdef INSTANCED
    gl_Position = uLightViewProj * (uModel * aInstModel * vec4(aPos, 1.0));
#else
    gl_Position = uLightViewProj * (uModel * vec4(aPos, 1.0));
#endif
}
