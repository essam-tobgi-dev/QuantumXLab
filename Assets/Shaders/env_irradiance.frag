#version 410 core
// Diffuse irradiance (spec 18 §4): cosine-lobe convolution of the environment on the GPU,
// stored as E(N)/π so the diffuse term is albedo · irradiance. Uniform hemisphere sweep in
// (φ, θ) over a small source mip (the lobe is very low-frequency); 64 × 16 = 1024 taps.
in vec3 vDir;
uniform samplerCube uEnv;
uniform float uSrcMip;     // source mip to integrate (≈ 32² face)
layout(location = 0) out vec4 oColor;
const float PI = 3.14159265359;
void main() {
    vec3 N = normalize(vDir);
    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 T = normalize(cross(up, N));
    vec3 B = cross(N, T);
    vec3 sum = vec3(0.0);
    float n = 0.0;
    const int NPHI = 64, NTHETA = 16;
    for (int i = 0; i < NPHI; ++i) {
        float phi = (float(i) + 0.5) / float(NPHI) * 2.0 * PI;
        for (int j = 0; j < NTHETA; ++j) {
            float theta = (float(j) + 0.5) / float(NTHETA) * 0.5 * PI;
            vec3 t = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            vec3 L = T * t.x + B * t.y + N * t.z;
            // dω = sinθ dθ dφ; weight cosθ; the normalisation by the same sum of cosθ sinθ
            // yields E/π exactly for a constant environment.
            float w = cos(theta) * sin(theta);
            sum += textureLod(uEnv, L, uSrcMip).rgb * w;
            n += w;
        }
    }
    oColor = vec4(sum / n, 1.0);
}
