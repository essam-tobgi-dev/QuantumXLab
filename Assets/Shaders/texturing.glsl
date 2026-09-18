// Spec 18 §4 (textures, amended 2026-09-18) — surface sampling for the TEXTURED permutation of
// pbr.frag. Units: 10 albedo (sRGB), 11 normal (linear, OpenGL +Y up), 12 ORM (R roughness,
// G metallic, B ao). uTex = (uv scale, triplanar flag, normal strength, textured flag);
// uTexGain = (1/mean albedo rgb, 1/mean roughness) or 1 when the material is not normalised.
uniform sampler2D uAlbedoMap;
uniform sampler2D uNormalMap;
uniform sampler2D uOrmMap;

// Per-pixel tangent frame from screen-space derivatives — the meshes carry no tangent
// attribute. T = ∂p/∂u and B = ∂p/∂v solved from (dp/dx, dp/dy) and (duv/dx, duv/dy)
// (Schüler, "Followup: Normal Mapping Without Precomputed Tangents", 2013).
mat3 cotangentFrame(vec3 N, vec3 p, vec2 uv) {
    vec3 dp1 = dFdx(p), dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv), duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = inversesqrt(max(max(dot(T, T), dot(B, B)), 1e-20));
    return mat3(T * invmax, B * invmax, N);
}

// Tangent-space normal with the material's strength applied to the in-plane components. The
// decode maps byte 128 to exactly 0 (255/127 scale), so a flat map leaves the normal untouched.
vec3 tangentNormal(vec2 uv) {
    vec3 n = texture(uNormalMap, uv).xyz * (255.0 / 127.0) - (128.0 / 127.0);
    n.xy *= uTex.z;
    return n;
}

struct Surface { vec3 albedo; vec3 orm; vec3 N; };

// Ng: unit geometric (interpolated, front-facing) normal.
Surface sampleSurface(vec3 Ng) {
    Surface s;
    if (uTex.y > 0.5) {
        // Triplanar: the world-space position (vWorld is camera-relative; uCameraPos restores the
        // origin) projected on the three axis planes, blended by |N|^4 normalised — for the room,
        // cans, frames and generated meshes whose uvs are poor. Tiles per metre = uTex.x. Planes
        // weighing under 1 % are skipped (a wall or a plate then costs one plane, not three);
        // the weights are renormalised so the blend stays continuous to that 1 %.
        vec3 p = (vWorld + uCameraPos.xyz) * uTex.x;
        vec3 w = pow(abs(Ng), vec3(4.0));
        w *= step(vec3(0.01), w / (w.x + w.y + w.z));
        w /= (w.x + w.y + w.z);
        s.albedo = vec3(0.0); s.orm = vec3(0.0);
        vec3 n = vec3(0.0);
        // Normal: "whiteout" blend (Golus 2017) — each plane's tangent normal gets the geometric
        // normal's in-plane components added, its z scaled by the plane's normal component,
        // then swizzled into world axes; a flat map reproduces Ng exactly on every plane.
        if (w.x > 0.0) {
            vec2 uv = p.zy;
            s.albedo += texture(uAlbedoMap, uv).rgb * w.x;
            s.orm += texture(uOrmMap, uv).rgb * w.x;
            vec3 t = tangentNormal(uv);
            t = vec3(t.xy + Ng.zy, abs(t.z) * Ng.x);
            n += t.zyx * w.x;
        }
        if (w.y > 0.0) {
            vec2 uv = p.xz;
            s.albedo += texture(uAlbedoMap, uv).rgb * w.y;
            s.orm += texture(uOrmMap, uv).rgb * w.y;
            vec3 t = tangentNormal(uv);
            t = vec3(t.xy + Ng.xz, abs(t.z) * Ng.y);
            n += t.xzy * w.y;
        }
        if (w.z > 0.0) {
            vec2 uv = p.xy;
            s.albedo += texture(uAlbedoMap, uv).rgb * w.z;
            s.orm += texture(uOrmMap, uv).rgb * w.z;
            vec3 t = tangentNormal(uv);
            t = vec3(t.xy + Ng.xy, abs(t.z) * Ng.z);
            n += t.xyz * w.z;
        }
        s.N = normalize(n);
    } else {
        vec2 uv = vUv * uTex.x;
        s.albedo = texture(uAlbedoMap, uv).rgb;
        s.orm = texture(uOrmMap, uv).rgb;
        s.N = normalize(cotangentFrame(Ng, vWorld, uv) * tangentNormal(uv));
    }
    return s;
}
