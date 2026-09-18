// Spec 12 §12 — `probe_entanglement`: von Neumann entropy of selected bipartitions, concurrence of
// pairs, and the mutual-information graph of spec 21 §3.8.
#include "Instruments/Probes.hpp"
#include "Instruments/StateAccess.hpp"
#include "QSim/Measures.hpp"
#include <format>

namespace qlab::instr {

SettingSchema EntanglementProbe::makeSchema() {
    SettingSchema s;
    s.id = "instr/probe_entanglement.schema.json";
    s.instrument = "probe_entanglement";
    s.settings = {
        SettingSpec::text("partitions", "", "Subsystems A of the `bipartition` channel, e.g. \"0;0,1;2,3\" (empty: every single qubit)"),
        SettingSpec::text("qubits", "", "Qubits of the pair channels (empty: all; required above 20 qubits)"),
        refreshRateSetting(),
    };
    return s;
}

EntanglementProbe::EntanglementProbe(std::uint32_t index) : ProbeBase({"probe_entanglement", index}, makeSchema()) {
    setProbeChannels({
        {{}, "entropy", "", "", FidelityClass::Exact, false, true, "S(ρ_q) in bits per qubit"},
        {{}, "bipartition", "", "", FidelityClass::Exact, false, true, "S(ρ_A) in bits for each selected subsystem A"},
        {{}, "concurrence", "", "", FidelityClass::Exact, false, true, "Concurrence of qubit pairs; aux `i`, `j`"},
        {{}, "mutual_information", "", "", FidelityClass::Exact, false, true, "I(i:j) = S_i + S_j − S_ij in bits; aux `i`, `j`"},
    });
}

Result<Trace> EntanglementProbe::doAcquire(const ChannelDesc& channel, AcquireContext& ctx) {
    QXL_TRY_ASSIGN(auto state, stateOf(ctx, id()));
    const qsim::Snapshot& s = *state;
    Trace t = makeTrace(channel, ctx);
    t.cls = s.cls;
    auto entropyOf = [&](std::span<const std::uint32_t> qubits) -> Result<double> {
        QXL_TRY_ASSIGN(num::Matrix rho, reducedDensity(s, qubits));
        return qsim::measures::entropyBits(rho);
    };
    if (channel.name == "entropy") {
        for (std::uint32_t q = 0; q < s.nQubits; ++q) {
            const std::uint32_t one[] = {q};
            QXL_TRY_ASSIGN(double e, entropyOf(one));
            t.x.push_back(static_cast<double>(q));
            t.y.push_back(e);
        }
        return t;
    }
    if (channel.name == "bipartition") {
        std::string text = ctx.settings.text("partitions");
        std::vector<std::vector<std::uint32_t>> sets;
        if (text.empty())
            for (std::uint32_t q = 0; q < s.nQubits; ++q) sets.push_back({q});
        std::string_view rest = text;
        while (!rest.empty()) {
            const auto semi = rest.find(';');
            const std::string_view item = rest.substr(0, semi);
            if (!item.empty()) {
                auto set = parseQubitList(item, s.nQubits);
                if (!set) return fail(err::BadInput, id().toString() + ": partitions: " + set.error().message);
                sets.push_back(std::move(*set));
            }
            if (semi == std::string_view::npos) break;
            rest.remove_prefix(semi + 1);
        }
        for (std::size_t k = 0; k < sets.size(); ++k) {
            QXL_TRY_ASSIGN(double e, entropyOf(sets[k]));
            t.x.push_back(static_cast<double>(k));
            t.y.push_back(e);
            std::string label = "S(";
            for (std::size_t j = 0; j < sets[k].size(); ++j) label += (j ? "," : "") + std::to_string(sets[k][j]);
            t.markers.push_back({static_cast<double>(k), e, label + ")", e, 0.0, "bit"});
        }
        return t;
    }
    // Pair channels: every pair of the selected qubits (spec 21 §3.8: all pairs only up to 20 qubits).
    auto qubits = parseQubitList(ctx.settings.text("qubits"), s.nQubits);
    if (!qubits) return fail(err::BadInput, id().toString() + ": qubits: " + qubits.error().message);
    if (qubits->size() > kMaxPairQubits)
        return fail(err::BadInput, std::format("{}: {} qubits selected; pick a subset of at most {} in `qubits`", id().toString(),
                                               qubits->size(), kMaxPairQubits));
    if (s.levels != 2 && channel.name == "concurrence")
        return fail(err::BadInput, id().toString() + ": concurrence is defined for two-level sites");
    auto& ai = t.aux["i"];
    auto& aj = t.aux["j"];
    std::vector<double> single(s.nQubits, 0.0);
    if (channel.name == "mutual_information")
        for (std::uint32_t q : *qubits) {
            const std::uint32_t one[] = {q};
            QXL_TRY_ASSIGN(single[q], entropyOf(one));
        }
    for (std::size_t a = 0; a < qubits->size(); ++a)
        for (std::size_t b = a + 1; b < qubits->size(); ++b) {
            const std::uint32_t pair[] = {(*qubits)[a], (*qubits)[b]};
            QXL_TRY_ASSIGN(num::Matrix rho, reducedDensity(s, pair));
            double value = 0.0;
            if (channel.name == "concurrence") {
                QXL_TRY_ASSIGN(value, qsim::measures::concurrence(rho));
            } else {
                value = single[pair[0]] + single[pair[1]] - qsim::measures::entropyBits(rho);
            }
            t.x.push_back(static_cast<double>(pair[0] * s.nQubits + pair[1]));
            t.y.push_back(value);
            ai.push_back(pair[0]);
            aj.push_back(pair[1]);
        }
    return t;
}

} // namespace qlab::instr
