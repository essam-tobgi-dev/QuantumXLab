#include "Cryo/Coax.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace qlab::cryo {

namespace {
// Areas from OD/ID and centre diameter (T08 §4.1), all in mm -> m².
CoaxSpec make(std::string id, double od, double outerId, double centre, std::string inner,
              std::string outer, double loss, bool sc = false) {
    CoaxSpec s;
    s.id = std::move(id);
    s.od_mm = od;
    const double mm2 = 1e-6;
    s.areaOuter_m2 = std::numbers::pi / 4.0 * (od * od - outerId * outerId) * mm2;
    s.areaInner_m2 = std::numbers::pi / 4.0 * centre * centre * mm2;
    s.areaDielectric_m2 = std::numbers::pi / 4.0 * (outerId * outerId - centre * centre) * mm2;
    s.innerMaterial = std::move(inner);
    s.outerMaterial = std::move(outer);
    s.loss_dB_per_m_5GHz = loss;
    s.superconductingOuter = sc;
    return s;
}
} // namespace

CoaxCatalog::CoaxCatalog() {
    // 0.86 mm class: OD 0.86 / ID 0.66 / centre 0.20 (UT-034 class); 2.19 mm class: 2.19/1.68/0.51;
    // 3.58 mm class (UT-141): 3.58/2.98/0.92.
    specs_.push_back(make("SS_086", 0.86, 0.66, 0.20, "stainless_304", "stainless_304", 8.0));
    specs_.push_back(make("SS_219", 2.19, 1.68, 0.51, "stainless_304", "stainless_304", 2.5));
    specs_.push_back(make("CuNi_086", 0.86, 0.66, 0.20, "CuNi_70_30", "CuNi_70_30", 7.0));
    specs_.push_back(make("CuNi_219", 2.19, 1.68, 0.51, "CuNi_70_30", "CuNi_70_30", 2.2));
    specs_.push_back(make("NbTi_086", 0.86, 0.66, 0.20, "NbTi", "NbTi", 0.02, true));
    specs_.push_back(make("Cu_086", 0.86, 0.66, 0.20, "Cu_OFHC_RRR50", "Cu_OFHC_RRR50", 2.0));
    specs_.push_back(make("Cu_141", 3.58, 2.98, 0.92, "Cu_OFHC_RRR50", "Cu_OFHC_RRR50", 0.9));
    // DC loom: 12 twisted pairs of 100 µm phosphor bronze; no dielectric model, no outer conductor.
    CoaxSpec loom;
    loom.id = "DC_loom_12";
    loom.od_mm = 0.1;
    loom.areaInner_m2 = std::numbers::pi / 4.0 * 0.1 * 0.1 * 1e-6;
    loom.innerMaterial = "phosphor_bronze";
    loom.dielectricMaterial = "";
    loom.outerMaterial = "";
    loom.conductors = 24;
    loom.loss_dB_per_m_5GHz = 0.0;
    specs_.push_back(loom);
}

const CoaxSpec* CoaxCatalog::find(std::string_view id) const {
    for (auto& s : specs_) if (s.id == id) return &s;
    return nullptr;
}
Result<const CoaxSpec*> CoaxCatalog::get(std::string_view id) const {
    if (auto* s = find(id)) return s;
    return fail(ErrorCode::Cryo_ + 10, std::format("unknown coax '{}'", id));
}
std::vector<std::string> CoaxCatalog::ids() const {
    std::vector<std::string> v; for (auto& s : specs_) v.push_back(s.id); return v;
}
void CoaxCatalog::add(CoaxSpec s) {
    for (auto& e : specs_) if (e.id == s.id) { e = std::move(s); return; }
    specs_.push_back(std::move(s));
}

Result<ConductionLoad> conductionLoad(const CoaxSpec& coax, double L, double Tc, double Th,
                                      const MaterialCatalog& mats) {
    if (L <= 0) return fail(ErrorCode::Cryo_ + 11, "coax length must be positive");
    ConductionLoad q;
    auto part = [&](const std::string& matId, double A, double& out) -> Status {
        if (matId.empty() || A <= 0) { out = 0; return {}; }
        auto m = mats.get(matId);
        if (!m) return std::unexpected(m.error());
        out = A / L * (*m)->conductivityIntegral(Tc, Th) * coax.conductors;
        return {};
    };
    QXL_TRY(part(coax.innerMaterial, coax.areaInner_m2, q.inner_W));
    QXL_TRY(part(coax.dielectricMaterial, coax.areaDielectric_m2, q.dielectric_W));
    QXL_TRY(part(coax.outerMaterial, coax.areaOuter_m2, q.outer_W));
    return q;
}

double coaxLoss_dB(const CoaxSpec& coax, double L, double f_Hz, double T_K) {
    if (coax.superconductingOuter && T_K < 9.2) return 0.02 * L;
    double scale = std::sqrt(std::max(f_Hz, 1.0) / 5e9);
    double cold = T_K < 4.5 ? 0.6 : (T_K < 60 ? 0.8 : 1.0);
    return coax.loss_dB_per_m_5GHz * scale * cold * L;
}

} // namespace qlab::cryo
