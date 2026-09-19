#include "Hardware/SystemModel.hpp"
#include "Hardware/Transmon.hpp"
#include <algorithm>
#include <format>

namespace qlab::hw {
using num::Complex;
using num::SparseMatrix;

namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;

// Per-site Duffing term in the rotating frame: (ω − ω_frame) n̂ + (α/2) n̂(n̂ − 1), rad/s.
SparseMatrix duffingSite(std::span<const std::uint32_t> dims, std::uint32_t site,
                         double detuningRad, double alphaRad) {
    const std::size_t d = dims[site];
    num::Matrix h(d, d);
    for (std::size_t k = 0; k < d; ++k) {
        const double n = static_cast<double>(k);
        h(k, k) = detuningRad * n + 0.5 * alphaRad * n * (n - 1.0);
    }
    return siteOperator(dims, site, h);
}
} // namespace

Result<SystemModelSpec> buildTransmonModel(const Device& dev, const Calibration& cal,
                                           const SystemModelOptions& opt) {
    if (!isTransmon(dev.technology))
        return fail(ErrorCode::Hardware_ + 20,
                    "buildTransmonModel called for a non-transmon device");
    if (opt.qubits.empty())
        return fail(ErrorCode::Hardware_ + 20, "no qubits selected for the system model");
    if (opt.levels < 2 || opt.levels > 6)
        return fail(ErrorCode::Hardware_ + 20,
                    std::format("transmon truncation must be 2..6 levels, got {}", opt.levels));

    SystemModelSpec m;
    m.rwa = opt.rwa;
    m.frame = opt.labFrame ? "lab" : "per_qubit_rotating";
    m.qubitIndices.assign(opt.qubits.begin(), opt.qubits.end());
    m.siteDims.assign(opt.qubits.size(), opt.levels);

    std::vector<std::string> bad;
    for (auto q : opt.qubits)
        if (!dev.hasQubit(q))
            bad.push_back(std::format("qubit {} is not on device '{}'", q, dev.id));
        else if (!cal.qubit(q))
            bad.push_back(std::format("no calibration for qubit {}", q));
    if (!bad.empty()) {
        Error e(ErrorCode::Hardware_ + 21, "cannot build the system model");
        e.notes = std::move(bad);
        return std::unexpected(std::move(e));
    }

    const std::size_t nSites = opt.qubits.size();
    const std::size_t D = m.dimension();
    m.h0 = SparseMatrix(D, D);
    m.frameFrequenciesHz.resize(nSites, 0.0);

    // ---- site terms
    for (std::size_t s = 0; s < nSites; ++s) {
        const QubitCal& qc = *cal.qubit(opt.qubits[s]);
        const double f01 = qc.f01.value.v;
        const double frame = opt.labFrame ? 0.0 : opt.frameFrequencyHz.value_or(f01);
        m.frameFrequenciesHz[s] = frame;
        const double detRad = kTwoPi * (f01 - frame);
        const double alphaRad = kTwoPi * qc.anharmonicity.value.v;
        m.h0 = num::add(m.h0,
                        duffingSite(m.siteDims, static_cast<std::uint32_t>(s), detRad, alphaRad));
    }

    // ---- exchange coupling g(a†b + ab†) between included, coupled pairs (T05 §5.1)
    if (opt.includeCoupling) {
        for (std::size_t i = 0; i < nSites; ++i)
            for (std::size_t j = i + 1; j < nSites; ++j) {
                const auto* ec = cal.edge(opt.qubits[i], opt.qubits[j]);
                if (!ec)
                    continue;
                double gHz = ec->couplingG.value.v;
                if (gHz == 0.0)
                    continue;
                // A tunable-coupler edge is idle-off: the calibrated ZZ fixes the residual
                // exchange.
                if (ec->coupler.has_value()) {
                    const QubitCal& a = *cal.qubit(opt.qubits[i]);
                    const QubitCal& b = *cal.qubit(opt.qubits[j]);
                    const double delta = a.f01.value.v - b.f01.value.v;
                    const double sum = a.anharmonicity.value.v + b.anharmonicity.value.v;
                    const double zz = ec->zz.value.v;
                    // Invert ζ = 2g²(α1+α2)/((Δ+α1)(Δ−α2)) for the idle |g_eff|.
                    const double denom = 2.0 * sum;
                    if (denom != 0.0) {
                        const double num2 = zz * (delta + a.anharmonicity.value.v) *
                                            (delta - b.anharmonicity.value.v) / denom;
                        gHz = num2 > 0.0 ? std::sqrt(num2) : 0.0;
                    } else
                        gHz = 0.0;
                }
                if (gHz == 0.0)
                    continue;
                const auto ai = siteAnnihilate(m.siteDims, static_cast<std::uint32_t>(i));
                const auto aj = siteAnnihilate(m.siteDims, static_cast<std::uint32_t>(j));
                auto term = num::add(num::matmul(ai.adjoint(), aj), num::matmul(ai, aj.adjoint()));
                m.h0 = num::add(m.h0, term.scale(kTwoPi * gHz));
            }
    }

    // ---- drives: one per site through its ladder operator (T07 §2)
    for (std::size_t s = 0; s < nSites; ++s) {
        const auto a = siteAnnihilate(m.siteDims, static_cast<std::uint32_t>(s));
        const auto ad = a.adjoint();
        DriveSpec d;
        d.channel = std::format("d[{}]", opt.qubits[s]);
        d.site = static_cast<std::uint32_t>(s);
        d.frameFrequencyHz = m.frameFrequenciesHz[s];
        d.inPhase = num::add(a, ad, 0.5, 0.5); // (a + a†)/2
        d.quadrature =
            num::add(a, ad, Complex(0, -0.5), Complex(0, 0.5)); // i(a† − a)/2 = +Y/2, T05 (7.1)
        m.drives.push_back(std::move(d));
    }
    // ---- cross-resonance channels: control site driven at the target frequency
    for (std::size_t i = 0; i < nSites; ++i)
        for (std::size_t j = 0; j < nSites; ++j) {
            if (i == j)
                continue;
            if (!dev.adjacent(opt.qubits[i], opt.qubits[j]))
                continue;
            if (!dev.nativeDirection(opt.qubits[i], opt.qubits[j]))
                continue;
            const auto a = siteAnnihilate(m.siteDims, static_cast<std::uint32_t>(i));
            const auto ad = a.adjoint();
            DriveSpec d;
            d.channel = std::format("u[{},{}]", opt.qubits[i], opt.qubits[j]);
            d.site = static_cast<std::uint32_t>(i);
            d.target = opt.qubits[j];
            d.frameFrequencyHz = cal.qubit(opt.qubits[j])->f01.value.v;
            d.inPhase = num::add(a, ad, 0.5, 0.5);
            d.quadrature =
                num::add(a, ad, Complex(0, -0.5), Complex(0, 0.5)); // i(a† − a)/2, T05 (7.1)
            m.drives.push_back(std::move(d));
        }

    if (opt.includeDecoherence) {
        for (std::size_t s = 0; s < nSites; ++s) {
            const QubitCal& qc = *cal.qubit(opt.qubits[s]);
            const auto idle = cal.idleParams(opt.qubits[s]);
            if (!idle)
                return std::unexpected(idle.error());
            const auto a = siteAnnihilate(m.siteDims, static_cast<std::uint32_t>(s));
            const double gamma1 = idle->t1.v > 0.0 ? 1.0 / idle->t1.v : 0.0;
            const double nth = std::clamp(qc.thermalPopulation.value, 0.0, 0.49);
            const double nbar = opt.includeThermal ? nth / (1.0 - 2.0 * nth) : 0.0;
            if (gamma1 > 0.0) {
                auto down = a;
                m.collapse.push_back({std::format("T1 q{}", opt.qubits[s]),
                                      down.scale(std::sqrt(gamma1 * (nbar + 1.0)))});
                if (nbar > 0.0) {
                    auto up = a.adjoint();
                    m.collapse.push_back({std::format("Tth q{}", opt.qubits[s]),
                                          up.scale(std::sqrt(gamma1 * nbar))});
                }
            }
            // Pure dephasing: L = √(γφ/2)·2n̂ reproduces exp(−t/Tφ) on the coherence (T04 §4).
            const double gphi = idle->tphi.v > 0.0 ? 1.0 / idle->tphi.v : 0.0;
            if (gphi > 0.0) {
                auto n = siteNumber(m.siteDims, static_cast<std::uint32_t>(s));
                m.collapse.push_back(
                    {std::format("Tphi q{}", opt.qubits[s]), n.scale(std::sqrt(2.0 * gphi))});
            }
            if (opt.includeLeakage && opt.levels > 2 && qc.leakage1q.value > 0.0) {
                const double dur = qc.duration1q.value.v > 0.0 ? qc.duration1q.value.v : 32e-9;
                const double rate = qc.leakage1q.value / dur;
                num::Matrix up21(opt.levels, opt.levels);
                up21(2, 1) = 1.0;
                auto op = siteOperator(m.siteDims, static_cast<std::uint32_t>(s), up21);
                m.collapse.push_back(
                    {std::format("leakage q{}", opt.qubits[s]), op.scale(std::sqrt(rate))});
            }
        }
    }
    return m;
}

} // namespace qlab::hw
