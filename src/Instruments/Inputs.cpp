#include "Instruments/Inputs.hpp"
#include "Units/Units.hpp"
#include <charconv>

namespace qlab::instr {
namespace {
const CryoCatalogs& sharedCatalogs() {
    static const CryoCatalogs
        catalogs; // immutable after construction: safe to share across threads
    return catalogs;
}

cryo::StageArray nominalTemperatures() {
    cryo::StageArray t{};
    for (cryo::Stage s : cryo::kStages)
        t[static_cast<std::size_t>(cryo::stageIndex(s))] = cryo::nominalTemperature(s);
    return t;
}

std::optional<std::uint32_t> parseIndex(std::string_view s) {
    std::uint32_t v = 0;
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || p != s.data() + s.size())
        return std::nullopt;
    return v;
}

OutputChain evaluateChain(const cryo::NoiseBudget& budget, const cryo::WiringLine& line,
                          const cryo::StageArray& temperatures, double frequencyHz) {
    const cryo::OutputChainNoise o = budget.outputLine(line, temperatures, frequencyHz);
    OutputChain c;
    c.lineId = line.id;
    // Spec 11 §6: T_sys = T_q + Σ T_N,i / G_before,i — the vacuum half photon is the first term, so
    // that η = T_q / T_sys reproduces T07 (8.2), η = ½ / (½ + n_add).
    c.tQuantumK = units::consts::h.v * frequencyHz / (2.0 * units::consts::k_B.v);
    c.tSysK = c.tQuantumK + o.T_sys_K;
    c.efficiency = c.tSysK > 0.0 ? c.tQuantumK / c.tSysK : 0.0;
    c.gainDb = o.gainTotal_dB;
    for (auto const& e : line.elements)
        if (e.kind == cryo::ElementKind::Preamp)
            c.hasPreamp = true;
    return c;
}
} // namespace

bool channelListCovers(std::string_view channel, std::uint32_t index) {
    const auto open = channel.find('[');
    const auto close = channel.rfind(']');
    if (open == std::string_view::npos || close == std::string_view::npos || close <= open + 1)
        return false;
    std::string_view inner = channel.substr(open + 1, close - open - 1);
    if (const auto dots = inner.find(".."); dots != std::string_view::npos) {
        auto lo = parseIndex(inner.substr(0, dots));
        auto hi = parseIndex(inner.substr(dots + 2));
        return lo && hi && index >= *lo && index <= *hi;
    }
    while (!inner.empty()) { // "3" or "0,1"
        const auto comma = inner.find(',');
        auto v = parseIndex(inner.substr(0, comma));
        if (v && *v == index)
            return true;
        if (comma == std::string_view::npos)
            break;
        inner.remove_prefix(comma + 1);
    }
    return false;
}

cryo::StageArray Environment::stageTemperatures() const {
    cryo::StageArray t = nominalTemperatures();
    for (std::size_t i = 0; i < t.size(); ++i)
        if (thermal.T_K[i] > 0.0)
            t[i] = thermal.T_K[i];
    return t;
}

const cryo::WiringLine* Environment::lineFor(cryo::LineKind kind, std::uint32_t qubit) const {
    if (!wiring)
        return nullptr;
    for (auto const& l : wiring->lines)
        if (l.kind == kind && channelListCovers(l.channel, qubit))
            return &l;
    return nullptr;
}

OutputChain Environment::outputChain(std::uint32_t qubit, double frequencyHz) const {
    const cryo::NoiseBudget& budget = catalogs ? catalogs->budget : sharedCatalogs().budget;
    if (const cryo::WiringLine* line = lineFor(cryo::LineKind::ReadoutOut, qubit))
        return evaluateChain(budget, *line, stageTemperatures(), frequencyHz);
    auto line = cryo::ChainCatalog::instantiate("readout_out_std", "readout_out_std", "a[*]");
    if (!line)
        return {};
    return evaluateChain(budget, *line, stageTemperatures(), frequencyHz);
}

double Environment::inputAttenuationDb(std::uint32_t qubit, double frequencyHz) const {
    const cryo::CoaxCatalog& coax = catalogs ? catalogs->coax : sharedCatalogs().coax;
    const cryo::StageArray t = stageTemperatures();
    auto total = [&](const cryo::WiringLine& line) {
        double db = line.totalAttenuation_dB();
        for (auto const& e : line.elements)
            if (e.kind == cryo::ElementKind::CoaxSegment)
                if (const cryo::CoaxSpec* c = coax.find(e.coax))
                    db += cryo::coaxLoss_dB(*c, e.length_m, frequencyHz,
                                            t[static_cast<std::size_t>(cryo::stageIndex(e.stage))]);
        return db;
    };
    if (const cryo::WiringLine* line = lineFor(cryo::LineKind::ReadoutIn, qubit))
        return total(*line);
    auto line = cryo::ChainCatalog::instantiate("readout_in_std", "readout_in_std", "m[*]");
    return line ? total(*line) : 70.0;
}

double Environment::electricalDelayS() const {
    const double v = cableVelocityFactor * units::consts::c.v;
    return v > 0.0 ? feedlineCableLengthM / v : 0.0;
}

OutputChain referenceOutputChain(double frequencyHz) {
    auto line = cryo::ChainCatalog::instantiate("readout_out_std", "readout_out_std", "a[*]");
    if (!line)
        return {};
    return evaluateChain(sharedCatalogs().budget, *line, nominalTemperatures(), frequencyHz);
}

void InputHub::publish(RunView v) {
    auto p = std::make_shared<const RunView>(std::move(v));
    std::lock_guard lk(mu_);
    run_ = std::move(p);
}
void InputHub::publish(Environment e) {
    auto p = std::make_shared<const Environment>(std::move(e));
    std::lock_guard lk(mu_);
    env_ = std::move(p);
}
std::shared_ptr<const RunView> InputHub::run() const {
    std::lock_guard lk(mu_);
    return run_;
}
std::shared_ptr<const Environment> InputHub::environment() const {
    std::lock_guard lk(mu_);
    return env_;
}

} // namespace qlab::instr
