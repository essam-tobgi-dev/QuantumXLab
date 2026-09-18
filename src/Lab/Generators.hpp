#pragma once
// Spec 17 §6 — procedural geometry. Every generator is a pure function of its parameters (the
// descriptor `geometry` object with layout-derived instance overrides layered on top) producing an
// indexed triangle mesh with unit normals and UVs. No mesh files. Output units are metres, or
// micrometres inside the ChipMicro scale island (GenContext::unitScale = 1e6).
#include "Core/Json.hpp"
#include "Graphics/Mesh.hpp"
#include "Lab/Types.hpp"
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace qlab::lab {

class GenParams {
public:
    GenParams() : j_(core::Json::object()) {}
    explicit GenParams(core::Json object);
    // Descriptor geometry with `overrides` (an object, may be null) applied key by key.
    static GenParams merged(const core::Json& descriptorGeometry, const core::Json& overrides);

    const core::Json& json() const { return j_; }
    GenParams& set(const std::string& key, core::Json value);
    bool has(std::string_view key) const;
    double number(std::string_view key, double fallback) const;
    int integer(std::string_view key, int fallback) const;
    bool boolean(std::string_view key, bool fallback) const;
    std::string string(std::string_view key, std::string_view fallback = {}) const;
    std::vector<std::string> strings(std::string_view key) const;
    // A length in metres from `<stem>_m`, `<stem>_mm` or `<stem>_um` (first present wins).
    std::optional<double> length(std::string_view stem) const;
    double length(std::string_view stem, double fallback_m) const;
    // A point / polyline in metres from `<stem>_m` or `<stem>_um`: [x,y,z] / [[x,y,z],...];
    // 2D entries [x,y] are chip-plane coordinates.
    std::optional<glm::dvec3> point3(std::string_view stem) const;
    std::vector<glm::dvec3> points3(std::string_view stem) const;
    std::vector<glm::dvec2> points2(std::string_view stem) const;
    // Canonical cache key (spec 17 §6: results cached by parameter hash).
    std::string cacheKey(std::string_view generator, Detail detail, double unitScale) const;

private:
    core::Json j_;
};

struct GenContext {
    Detail detail = Detail::Full;
    double unitScale = 1.0; // output units per metre
    double S(double metres) const { return metres * unitScale; }
    float F(double metres) const { return static_cast<float>(metres * unitScale); }
};

using GeneratorFn = Result<gfx::MeshData> (*)(const GenParams&, const GenContext&);

// The generator names of spec 17 §6 in use by the catalog (TrapChip/Octagon for §3.6, Post for the
// stage support posts of §3.1).
std::span<const std::string_view> generatorNames();
bool hasGenerator(std::string_view name);
// Runs a generator. Detail::Hidden yields an empty mesh. The result is oriented so triangle winding
// agrees with the normals and validated (finite positions, unit normals, index range).
Result<gfx::MeshData> generateMesh(std::string_view generator, const GenParams& params, const GenContext& ctx);

// Nominal envelopes of generators without size parameters (metres).
inline constexpr double kSmaLength_m = 0.020;
inline constexpr double kSmaHexAcrossCorners_m = 0.00924; // 8 mm across flats
inline constexpr double kRackPanelWidth_m = 0.4826;       // 19-inch panel
inline constexpr double kRackUnit_m = 0.04445;            // 1U
inline constexpr double kRackUnitDepth_m = 0.55;
inline constexpr double kTrapChipSize_m[3] = {0.010, 0.0005, 0.003};
inline constexpr double kGantryHeight_m = 0.35;
inline constexpr double kTrayWidth_m = 0.30, kTrayHeight_m = 0.06;

// Stage plates (spec 17 §3.1, fridge detail pass): 1 mm rim chamfer, bolt circle 20 mm inside the
// rim with one M6 counterbore per ≈ 45 mm of circumference (24..36), where the support posts
// and the shield flanges bolt on.
inline constexpr double kPlateChamfer_m = 0.001;
inline constexpr double kPlateBoltInset_m = 0.020;
inline double plateBoltRing_m(double r_m) { return r_m - kPlateBoltInset_m; }
// 24 or 36 so that the six posts (every 60°, from 30°) land on counterbores.
inline int plateBoltCount(double r_m) {
    double n = 2.0 * 3.14159265358979 * plateBoltRing_m(r_m) / 0.045;
    return n < 30.0 ? 24 : 36;
}

// Chip film stack heights above the substrate (metres). Films are 100–200 nm thick physically;
// the stack is exaggerated to micrometres so the layers survive depth-buffer quantisation at
// bookmark distances (Illustrative).
inline constexpr double kFilmGround_m = 2e-6;
inline constexpr double kFilmGapTop_m = 4e-6;
inline constexpr double kFilmMetalTop_m = 6e-6;
inline constexpr double kFilmJunction_m = 0.3e-6;

// Spec 17 §6 — λ/4 CPW resonator length L = c / (4 f_r sqrt(ε_eff)), ε_eff ≈ (1 + ε_r)/2 = 6.45 (Si).
double quarterWaveLength_m(double f_r_Hz, double epsEff = 6.45);

// Spec 17 §6 Meander(L, pitch, w, s, turns): chip-plane centreline from the origin along +x — a
// lead, turns = floor((L − 2 lead)/(pitch + 2 amplitude)) excursions advancing by `pitch`, and a
// closing lead. The excursion amplitude is widened so the centreline length is exactly L
// (any unit; empty when L ≤ 2 lead + pitch).
std::vector<glm::dvec2> meanderCenterline(double L, double pitch, double amplitude, double lead);

// Chip-plane (x, y) → local mesh plane (X, Z) = (x, −y): chip "north" faces −Z.
inline glm::vec2 chipToXZ(glm::dvec2 p, double scale) {
    return {static_cast<float>(p.x * scale), static_cast<float>(-p.y * scale)};
}

namespace gen {
// Generator entry points (one translation unit per family).
Result<gfx::MeshData> box(const GenParams&, const GenContext&);
Result<gfx::MeshData> cylinder(const GenParams&, const GenContext&);
Result<gfx::MeshData> sphere(const GenParams&, const GenContext&);
Result<gfx::MeshData> torus(const GenParams&, const GenContext&);
Result<gfx::MeshData> octagon(const GenParams&, const GenContext&);
Result<gfx::MeshData> plate(const GenParams&, const GenContext&);
Result<gfx::MeshData> plateWithHoles(const GenParams&, const GenContext&);
Result<gfx::MeshData> can(const GenParams&, const GenContext&);
Result<gfx::MeshData> post(const GenParams&, const GenContext&);
Result<gfx::MeshData> frame(const GenParams&, const GenContext&);
Result<gfx::MeshData> extrudedU(const GenParams&, const GenContext&);
Result<gfx::MeshData> rackUnit(const GenParams&, const GenContext&);
Result<gfx::MeshData> tube(const GenParams&, const GenContext&);
Result<gfx::MeshData> spiral(const GenParams&, const GenContext&);
Result<gfx::MeshData> cylinderSma(const GenParams&, const GenContext&);
Result<gfx::MeshData> smaConnector(const GenParams&, const GenContext&);
Result<gfx::MeshData> wirebondArc(const GenParams&, const GenContext&);
Result<gfx::MeshData> cpw(const GenParams&, const GenContext&);
Result<gfx::MeshData> meander(const GenParams&, const GenContext&);
Result<gfx::MeshData> xmon(const GenParams&, const GenContext&);
Result<gfx::MeshData> junction(const GenParams&, const GenContext&);
Result<gfx::MeshData> squidLoop(const GenParams&, const GenContext&);
Result<gfx::MeshData> airbridge(const GenParams&, const GenContext&);
Result<gfx::MeshData> trapChip(const GenParams&, const GenContext&);
} // namespace gen

} // namespace qlab::lab
