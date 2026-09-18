#version 410 core
// Cube-face pass (spec 18 §4 IBL): a fullscreen quad whose corners carry the world direction of
// the cube face `uFace` (GL cubemap convention, +X..-Z). The direction interpolates linearly
// across the face plane, so the fragment shader only normalizes it.
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec2 aUv;
uniform int uFace;
out vec3 vDir;
void main() {
    vec2 st = aUv * 2.0 - 1.0;   // -1..1 across the face
    vec3 d;
    // Cubemap face layouts (GL spec table 8.19): (s, t) → direction, t grows downward in texel rows.
    if      (uFace == 0) d = vec3( 1.0, -st.y, -st.x);
    else if (uFace == 1) d = vec3(-1.0, -st.y,  st.x);
    else if (uFace == 2) d = vec3( st.x,  1.0,  st.y);
    else if (uFace == 3) d = vec3( st.x, -1.0, -st.y);
    else if (uFace == 4) d = vec3( st.x, -st.y,  1.0);
    else                 d = vec3(-st.x, -st.y, -1.0);
    vDir = d;
    gl_Position = vec4(aPos.xy, 0.0, 1.0);
}
