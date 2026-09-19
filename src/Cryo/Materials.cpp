#include "Cryo/Materials.hpp"
#include "Core/Json.hpp"
#include "Core/Paths.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace qlab::cryo {

namespace {
constexpr double kR_JmolK = 8.314462618;

// Debye function D(x) = 3/x^3 ∫_0^x t^4 e^t/(e^t-1)^2 dt, Simpson on 200 panels.
double debyeD(double x) {
    if (x <= 0)
        return 1.0;
    if (x > 200.0)
        return 4.0 * std::pow(std::numbers::pi, 4) / (5.0 * x * x * x); // T^3 law
    const int n = 200;
    double h = x / n, s = 0.0;
    auto f = [](double t) {
        if (t < 1e-6)
            return t * t;
        double e = std::exp(t);
        double d = e - 1.0;
        return t * t * t * t * e / (d * d);
    };
    for (int i = 0; i <= n; ++i) {
        double w = (i == 0 || i == n) ? 1.0 : (i % 2 ? 4.0 : 2.0);
        s += w * f(i * h);
    }
    s *= h / 3.0;
    return 3.0 / (x * x * x) * s;
}
} // namespace

double debyeSpecificHeat(double T, double thetaD, double molarMass_g, double gamma_mJ_molK2) {
    if (T <= 0)
        return 0.0;
    double cvLattice_molar = 3.0 * kR_JmolK * debyeD(thetaD / T); // J/mol/K
    double cElec_molar = gamma_mJ_molK2 * 1e-3 * T;               // J/mol/K
    return (cvLattice_molar + cElec_molar) / (molarMass_g * 1e-3);
}

double Material::k(double T) const {
    if (kTable.empty())
        return 0.0;
    if (T <= kTable.front().first) {
        double r = T / kTable.front().first;
        return kTable.front().second * (superconducting ? r * r * r : r);
    }
    if (T >= kTable.back().first)
        return kTable.back().second;
    double x = std::log(T);
    auto it = std::lower_bound(kTable.begin(), kTable.end(), T,
                               [](const auto& p, double v) { return p.first < v; });
    auto hi = it, lo = it - 1;
    double x0 = std::log(lo->first), x1 = std::log(hi->first);
    double f = (x - x0) / (x1 - x0);
    return lo->second + f * (hi->second - lo->second);
}

double Material::cumulativeIntegral(double T) const {
    constexpr double T0 = 1e-4, T1 = 400.0;
    constexpr int N = 4000;
    if (cumGrid_.empty()) {
        cumGrid_.resize(N + 1);
        cumVal_.resize(N + 1);
        double a = std::log(T0), b = std::log(T1), h = (b - a) / N;
        double acc = 0.0;
        cumGrid_[0] = T0;
        cumVal_[0] = 0.0;
        for (int i = 1; i <= N; ++i) {
            double Tm = std::exp(a + (i - 0.5) * h);
            acc += k(Tm) * Tm * h; // ∫ k dT = ∫ k T d(ln T), midpoint per cell
            cumGrid_[i] = std::exp(a + i * h);
            cumVal_[i] = acc;
        }
    }
    T = std::clamp(T, T0, T1);
    double a = std::log(T0), b = std::log(T1);
    double x = (std::log(T) - a) / (b - a) * N;
    int i = std::clamp(static_cast<int>(x), 0, N - 1);
    double f = x - i;
    return cumVal_[i] + f * (cumVal_[i + 1] - cumVal_[i]);
}

double Material::conductivityIntegral(double Tc, double Th) const {
    if (Th <= Tc)
        return 0.0;
    return cumulativeIntegral(Th) - cumulativeIntegral(std::max(Tc, 1e-4));
}

double Material::specificHeat(double T) const {
    if (cp.model == "table" && !cp.table.empty()) {
        if (T <= cp.table.front().first)
            return cp.table.front().second * std::pow(T / cp.table.front().first, 3);
        if (T >= cp.table.back().first)
            return cp.table.back().second;
        auto it = std::lower_bound(cp.table.begin(), cp.table.end(), T,
                                   [](const auto& p, double v) { return p.first < v; });
        auto hi = it, lo = it - 1;
        double f = (T - lo->first) / (hi->first - lo->first);
        return lo->second + f * (hi->second - lo->second);
    }
    return debyeSpecificHeat(T, cp.thetaD_K, cp.molarMass_g, cp.gamma_mJ_molK2);
}

double Material::enthalpy(double Tlo, double Thi) const {
    if (Thi <= Tlo)
        return 0.0;
    const int n = 400;
    double h = (Thi - Tlo) / n, s = 0.0;
    for (int i = 0; i < n; ++i)
        s += specificHeat(Tlo + (i + 0.5) * h) * h;
    return s;
}

Result<Material> MaterialCatalog::parse(const std::string& text) {
    auto env = core::JsonEnvelope::parse(text, "qlab.material");
    if (!env)
        return std::unexpected(env.error());
    const core::Json& d = env->data;
    Material m;
    m.id = d.value("id", "");
    if (m.id.empty())
        return fail(ErrorCode::Cryo_ + 1, "material without id");
    m.source = d.value("source", "");
    if (!d.contains("k_table") || !d["k_table"].is_array() || d["k_table"].size() < 2)
        return fail(ErrorCode::Cryo_ + 2, "material '" + m.id + "': k_table missing or too short");
    for (const auto& row : d["k_table"]) {
        if (!row.is_array() || row.size() != 2)
            return fail(ErrorCode::Cryo_ + 2, "material '" + m.id + "': bad k_table row");
        m.kTable.emplace_back(row[0].get<double>(), row[1].get<double>());
    }
    std::sort(m.kTable.begin(), m.kTable.end());
    if (d.contains("specific_heat")) {
        const auto& s = d["specific_heat"];
        m.cp.model = s.value("model", "debye");
        m.cp.thetaD_K = s.value("theta_d_K", 300.0);
        m.cp.gamma_mJ_molK2 = s.value("gamma_mJ_molK2", 0.0);
        m.cp.molarMass_g = s.value("molar_mass_g", 60.0);
        if (s.contains("table"))
            for (const auto& row : s["table"])
                m.cp.table.emplace_back(row[0].get<double>(), row[1].get<double>());
    }
    m.density_kg_m3 = d.value("density_kg_m3", 1000.0);
    m.emissivity = d.value("emissivity", 0.1);
    if (d.contains("superconducting")) {
        const auto& sc = d["superconducting"];
        if (sc.is_boolean()) {
            m.superconducting = sc.get<bool>();
            m.Tc_K = 9.2;
        } else if (sc.is_object()) {
            m.superconducting = true;
            m.Tc_K = sc.value("Tc_K", 9.2);
        } else if (sc.is_number()) {
            m.superconducting = true;
            m.Tc_K = sc.get<double>();
        }
    }
    return m;
}

Result<MaterialCatalog> MaterialCatalog::load(const std::filesystem::path& dirIn) {
    std::filesystem::path dir = dirIn.empty() ? core::assetDir() / "Lab" / "Materials" : dirIn;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec))
        return fail(ErrorCode::Cryo_ + 3, "materials directory not found: " + dir.string());
    MaterialCatalog cat;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.path().extension() != ".json")
            continue;
        auto text = core::readTextFile(entry.path());
        if (!text)
            return std::unexpected(text.error());
        auto m = parse(*text);
        if (!m) {
            m.error().notes.push_back("file: " + entry.path().string());
            return std::unexpected(m.error());
        }
        cat.add(std::move(*m));
    }
    if (cat.size() == 0)
        return fail(ErrorCode::Cryo_ + 3, "no materials in " + dir.string());
    return cat;
}

const Material* MaterialCatalog::find(std::string_view id) const {
    auto it = mats_.find(id);
    return it == mats_.end() ? nullptr : &it->second;
}
Result<const Material*> MaterialCatalog::get(std::string_view id) const {
    if (auto* m = find(id))
        return m;
    return fail(ErrorCode::Cryo_ + 4, std::format("unknown material '{}'", id));
}
std::vector<std::string> MaterialCatalog::ids() const {
    std::vector<std::string> v;
    for (auto& [k, _] : mats_)
        v.push_back(k);
    return v;
}

} // namespace qlab::cryo
