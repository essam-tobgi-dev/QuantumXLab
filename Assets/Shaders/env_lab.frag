#version 410 core
// Procedural laboratory environment (spec 18 §4): linear radiance as a pure function of direction.
// What a polished part reflects is what makes it read as metal. The room is lit the way a product
// photograph is lit: white walls at the radiance a lit white wall has (≈ 0.6 under E/π ≈ 1),
// large soft light panels (radiance 6 over ≈ 14 % of the ceiling) that a gold plate seen from
// above reflects as a bright warm sheet, a darker bench/rack band and floor (≈ 0.25) that the same
// plate seen from the side reflects as its deep body colour, a bright strip high on one wall for
// a long horizontal highlight. The cosine-weighted upward irradiance E/π ≈ 1.4, so the App runs
// at exposure ≈ 0.55. Blends between faces are smooth (no seams in the prefiltered mips); tints
// are subtle so grey plastics stay grey under the irradiance.
in vec3 vDir;
layout(location = 0) out vec4 oColor;

float panel(vec2 p, vec2 c, vec2 half_) {
    // soft-edged rectangle (edge width 0.09 in ceiling units: a diffuser, not a bare tube — and
    // wide enough that the roughest prefiltered mip stays smooth) centred at c
    vec2 d = abs(p - c) - half_;
    float m = max(d.x, d.y);
    return 1.0 - smoothstep(-0.09, 0.09, m);
}

void main() {
    vec3 d = normalize(vDir);
    float horiz = max(abs(d.x), abs(d.z));
    float wUp = smoothstep(-0.12, 0.12, d.y - horiz);      // ceiling weight
    float wDown = smoothstep(-0.12, 0.12, -d.y - horiz);   // floor weight
    float wWall = 1.0 - wUp - wDown;

    // Walls: neutral ≈ 0.22, a 6 % warm→cool drift across x, brighter toward the ceiling where
    // the panels light them, a dark bench/rack band below the horizon, and vertical banding
    // (cabinets, racks) so a reflection carries structure rather than a flat tone.
    float warm = 0.5 + 0.5 * d.x;
    vec3 wall = mix(vec3(0.56, 0.57, 0.60), vec3(0.62, 0.58, 0.53), warm);
    float elev = d.y / max(horiz, 1e-4);                   // −1 floor … 1 ceiling on the wall plane
    wall *= 0.85 + 0.35 * smoothstep(-0.2, 1.0, elev);
    wall *= mix(0.5, 1.0, smoothstep(-0.25, -0.05, elev));
    float az = atan(d.z, d.x);
    wall *= 1.0 + 0.10 * sin(az * 6.0) * (1.0 - abs(elev));
    // A bright window-like strip high on the −z wall: a long highlight for horizontal surfaces.
    wall += vec3(1.6, 1.6, 1.7) * smoothstep(0.45, 0.75, elev) * (1.0 - smoothstep(0.75, 0.98, elev)) *
            smoothstep(-0.35, 0.35, -d.z - abs(d.x)) * 0.5;

    // Ceiling: base 0.30 with four 0.16 × 0.11 panels of radiance 40 at the quadrants of the
    // plane y = 1 (≈ 1.8 % of the plane).
    vec3 ceil = vec3(0.62, 0.62, 0.64);
    if (d.y > 1e-3) {
        vec2 p = d.xz / d.y;
        float hit = 0.0;
        hit += panel(p, vec2( 0.7,  0.7), vec2(0.24, 0.17));
        hit += panel(p, vec2(-0.7,  0.7), vec2(0.24, 0.17));
        hit += panel(p, vec2( 0.7, -0.7), vec2(0.24, 0.17));
        hit += panel(p, vec2(-0.7, -0.7), vec2(0.24, 0.17));
        ceil = mix(ceil, vec3(6.0, 5.95, 5.8), clamp(hit, 0.0, 1.0));
    }

    // Floor: dark epoxy ≈ 0.07 with the soft reflection of the panels (it is glossy in the lab).
    vec3 floorC = vec3(0.22, 0.225, 0.235);
    if (d.y < -1e-3) {
        vec2 p = d.xz / -d.y;
        float hit = 0.0;
        hit += panel(p, vec2( 0.7,  0.7), vec2(0.16, 0.11));
        hit += panel(p, vec2(-0.7,  0.7), vec2(0.16, 0.11));
        hit += panel(p, vec2( 0.7, -0.7), vec2(0.16, 0.11));
        hit += panel(p, vec2(-0.7, -0.7), vec2(0.16, 0.11));
        floorC += vec3(0.25) * clamp(hit, 0.0, 1.0);
    }

    vec3 L = wall * wWall + ceil * wUp + floorC * wDown;
    oColor = vec4(L, 1.0);
}
