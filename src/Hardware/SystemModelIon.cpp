#include "Hardware/IonChain.hpp"
#include "Hardware/SystemModel.hpp"
#include <algorithm>
#include <format>

namespace qlab::hw {
using num::Complex;
using num::SparseMatrix;

namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;

num::Matrix pauliMinus() { // σ⁻ = |0⟩⟨1| (T04: lowering toward the ground state |0⟩)
    num::Matrix m(2, 2);
    m(0, 1) = 1.0;
    return m;
}
num::Matrix pauliZ() {
    num::Matrix m(2, 2);
    m(0, 0) = 1.0;
    m(1, 1) = -1.0;
    return m;
}
num::Matrix pauliX() {
    num::Matrix m(2, 2);
    m(0, 1) = 1.0;
    m(1, 0) = 1.0;
    return m;
}
num::Matrix pauliY() {
    num::Matrix m(2, 2);
    m(0, 1) = Complex(0.0, -1.0);
    m(1, 0) = Complex(0.0, 1.0);
    return m;
}
} // namespace

Result<SystemModelSpec> buildIonModel(const Device& dev, const Calibration& cal,
                                      const SystemModelOptions& opt) {
    if (dev.technology != Technology::IonChain)
        return fail(ErrorCode::Hardware_ + 22, "buildIonModel called for a non-ion device");
    if (opt.qubits.empty())
        return fail(ErrorCode::Hardware_ + 22, "no ions selected for the system model");

    SystemModelSpec m;
    m.rwa = opt.rwa;
    m.frame = opt.labFrame ? "lab" : "per_qubit_rotating";
    m.qubitIndices.assign(opt.qubits.begin(), opt.qubits.end());
    // Sites: the ion qubits (2 levels each, little-endian) then the motional mode (Fock cutoff).
    const std::size_t nIons = opt.qubits.size();
    const int fock = std::max(2, opt.fockCutoff);
    m.siteDims.assign(nIons, 2);
    m.siteDims.push_back(static_cast<std::uint32_t>(fock));
    const std::uint32_t modeSite = static_cast<std::uint32_t>(nIons);

    std::vector<std::string> bad;
    for (auto q : opt.qubits)
        if (!dev.hasQubit(q)) bad.push_back(std::format("ion {} is not on device '{}'", q, dev.id));
        else if (!cal.qubit(q)) bad.push_back(std::format("no calibration for ion {}", q));
    if (!bad.empty()) {
        Error e(ErrorCode::Hardware_ + 23, "cannot build the ion system model");
        e.notes = std::move(bad);
        return std::unexpected(std::move(e));
    }

    // Gate mode: the calibration's motional.gate_mode names the axis and index (axial COM on the
    // 11-ion chain, radial COM on the 32-ion chain); fall back to the axial COM frequency.
    double modeHz = dev.motionalModes ? dev.motionalModes->omegaZ.v : 0.3e6;
    if (cal.motional) {
        if (auto f = cal.motional->gateModeFrequency()) modeHz = f->v;
        else if (!cal.motional->axialModes.empty()) modeHz = cal.motional->axialModes[0].v;
    }

    const std::size_t D = m.dimension();
    m.h0 = SparseMatrix(D, D);
    m.frameFrequenciesHz.assign(nIons, 0.0);

    // Qubit frames: each ion rotates at its own transition, so only the mode remains in H0.
    for (std::size_t s = 0; s < nIons; ++s) {
        const QubitCal& qc = *cal.qubit(opt.qubits[s]);
        const double f = qc.f01.value.v;
        const double frame = opt.labFrame ? 0.0 : opt.frameFrequencyHz.value_or(f);
        m.frameFrequenciesHz[s] = frame;
        if (opt.labFrame || frame != f) {
            auto z = siteOperator(m.siteDims, static_cast<std::uint32_t>(s), pauliZ());
            // H = −(ħ/2)(ω − ω_frame) σ_z with |1⟩ excited (T04 §1).
            m.h0 = num::add(m.h0, z.scale(-0.5 * kTwoPi * (f - frame)));
        }
    }
    m.frameFrequenciesHz.push_back(0.0); // the motional mode is kept in the lab frame
    // H_mode = ω_m a†a
    m.h0 = num::add(m.h0, siteNumber(m.siteDims, modeSite).scale(kTwoPi * modeHz));

    // Drives: per-ion Raman carrier (σ_x/σ_y quadratures) plus the bichromatic MS channels.
    for (std::size_t s = 0; s < nIons; ++s) {
        DriveSpec d;
        d.channel = std::format("r[{}]", opt.qubits[s]);
        d.site = static_cast<std::uint32_t>(s);
        d.frameFrequencyHz = m.frameFrequenciesHz[s];
        d.inPhase = siteOperator(m.siteDims, static_cast<std::uint32_t>(s), pauliX()).scale(0.5);
        d.quadrature = siteOperator(m.siteDims, static_cast<std::uint32_t>(s), pauliY()).scale(0.5);
        m.drives.push_back(std::move(d));
    }
    // MS channel on each included pair: spin-dependent force S_φ(a e^{-iδt} + a† e^{iδt}) with
    // coupling g = ηΩ/2 (T06 §6.1). The envelope carries Ω and the detuning phase.
    const auto a = siteAnnihilate(m.siteDims, modeSite);
    const auto ad = a.adjoint();
    for (std::size_t i = 0; i < nIons; ++i)
        for (std::size_t j = i + 1; j < nIons; ++j) {
            const auto* qi = cal.qubit(opt.qubits[i]);
            const auto* qj = cal.qubit(opt.qubits[j]);
            // `lamb_dicke` is the calibrated effective Lamb–Dicke parameter of ion n in the gate mode
            // (participation included); Pulse's MsParams reads the same field, so the amplitude it
            // plays and the operator it drives agree by construction.
            const double etaI = qi && qi->lambDicke ? qi->lambDicke->value : dev.ion ? dev.ion->lambDickeNominal : 0.08;
            const double etaJ = qj && qj->lambDicke ? qj->lambDicke->value : etaI;
            // Spin-dependent force per unit envelope F(t) = 2Ω cos(μt), mode in its lab frame:
            //   H = Σ_n (η_n/2) (Re F σ_x⁽ⁿ⁾ + Im F σ_y⁽ⁿ⁾)(a + a†)      (T06 (6.1), g = ηΩ/2)
            // Each ion carries its own η_n, so the entangling angle scales as η_i η_j exactly.
            auto sx = num::add(siteOperator(m.siteDims, static_cast<std::uint32_t>(i), pauliX()),
                               siteOperator(m.siteDims, static_cast<std::uint32_t>(j), pauliX()), etaI, etaJ);
            auto sy = num::add(siteOperator(m.siteDims, static_cast<std::uint32_t>(i), pauliY()),
                               siteOperator(m.siteDims, static_cast<std::uint32_t>(j), pauliY()), etaI, etaJ);
            DriveSpec d;
            d.channel = std::format("ms[{},{}]", opt.qubits[i], opt.qubits[j]);
            d.site = static_cast<std::uint32_t>(i);
            d.target = opt.qubits[j];
            d.frameFrequencyHz = modeHz;
            auto aPlus = num::add(a, ad, 1.0, 1.0); // a + a†
            d.inPhase = num::matmul(sx, aPlus).scale(0.5);
            d.quadrature = num::matmul(sy, aPlus).scale(0.5);
            m.drives.push_back(std::move(d));
        }

    if (opt.includeDecoherence) {
        for (std::size_t s = 0; s < nIons; ++s) {
            const auto idle = cal.idleParams(opt.qubits[s]);
            if (!idle) return std::unexpected(idle.error());
            const double gamma1 = idle->t1.v > 0.0 ? 1.0 / idle->t1.v : 0.0;
            if (gamma1 > 1e-12) {
                auto sm = siteOperator(m.siteDims, static_cast<std::uint32_t>(s), pauliMinus());
                m.collapse.push_back({std::format("T1 q{}", opt.qubits[s]), sm.scale(std::sqrt(gamma1))});
            }
            const double gphi = idle->tphi.v > 0.0 ? 1.0 / idle->tphi.v : 0.0;
            if (gphi > 1e-12) {
                auto z = siteOperator(m.siteDims, static_cast<std::uint32_t>(s), pauliZ());
                m.collapse.push_back({std::format("Tphi q{}", opt.qubits[s]),
                                      z.scale(std::sqrt(0.5 * gphi))});
            }
        }
        // Motional heating: L = √(ṅ) a† (T06 §8).
        double heating = dev.motionalModes ? dev.motionalModes->heatingQuantaPerS : 0.0;
        if (cal.motional && cal.motional->heatingQuantaPerS) heating = *cal.motional->heatingQuantaPerS; // calibrated value wins
        if (heating > 0.0) {
            auto up = ad;
            m.collapse.push_back({"heating", up.scale(std::sqrt(heating))});
        }
    }
    return m;
}

Result<SystemModelSpec> buildSystemModel(const Device& dev, const Calibration& cal,
                                         const SystemModelOptions& opt) {
    if (dev.technology == Technology::IonChain) {
        // Cap: N qubits ⊗ Fock cutoff (spec 07 §5 keeps the Lindblad space small).
        if (opt.qubits.size() > 6)
            return fail(ErrorCode::Hardware_ + 24,
                        std::format("pulse-level ion models are limited to 6 ions, got {}", opt.qubits.size()));
        return buildIonModel(dev, cal, opt);
    }
    if (opt.qubits.size() > 5)
        return fail(ErrorCode::Hardware_ + 24,
                    std::format("pulse-level transmon models are limited to 5 qubits, got {}",
                                opt.qubits.size()));
    return buildTransmonModel(dev, cal, opt);
}

} // namespace qlab::hw
