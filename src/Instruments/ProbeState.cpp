// Spec 12 §12 — ProbeBase and `probe_state`.
#include "Instruments/Probes.hpp"
#include "Instruments/StateAccess.hpp"
#include "Numerics/Numerics.hpp"
#include "QSim/Measures.hpp"
#include <charconv>
#include <cmath>
#include <format>

namespace qlab::instr {

void ProbeBase::setProbeChannels(std::vector<ChannelDesc> channels) {
    for (auto& c : channels)
        c.simulatorOnly = true;
    setChannels(std::move(channels));
}

Result<std::shared_ptr<const qsim::Snapshot>> ProbeBase::stateOf(const AcquireContext& ctx,
                                                                 const InstrumentId& id) {
    if (!ctx.run || !ctx.run->state)
        return fail(err::NotBound, id.toString() + ": the run view holds no state snapshot");
    return ctx.run->state;
}

Result<std::vector<std::uint32_t>> parseQubitList(std::string_view text, std::uint32_t nQubits) {
    std::vector<std::uint32_t> out;
    while (!text.empty()) {
        const auto comma = text.find(',');
        std::string_view item = text.substr(0, comma);
        while (!item.empty() && item.front() == ' ')
            item.remove_prefix(1);
        while (!item.empty() && item.back() == ' ')
            item.remove_suffix(1);
        if (!item.empty()) {
            std::uint32_t q = 0;
            auto [p, ec] = std::from_chars(item.data(), item.data() + item.size(), q);
            if (ec != std::errc{} || p != item.data() + item.size())
                return fail(err::BadInput, std::format("'{}' is not a qubit index", item));
            if (q >= nQubits)
                return fail(err::BadInput,
                            std::format("qubit {} is outside the {}-qubit register", q, nQubits));
            out.push_back(q);
        }
        if (comma == std::string_view::npos)
            break;
        text.remove_prefix(comma + 1);
    }
    if (out.empty())
        for (std::uint32_t q = 0; q < nQubits; ++q)
            out.push_back(q);
    return out;
}

SettingSchema StateProbe::makeSchema() {
    SettingSchema s;
    s.id = "instr/probe_state.schema.json";
    s.instrument = "probe_state";
    s.settings = {
        SettingSpec::text("qubits", "",
                          "Qubits of the `reduced` channel, e.g. \"0,1\" (empty: all)"),
        refreshRateSetting(),
    };
    return s;
}

StateProbe::StateProbe(std::uint32_t index) : ProbeBase({"probe_state", index}, makeSchema()) {
    setProbeChannels({
        {{},
         "amplitudes",
         "",
         "",
         FidelityClass::Exact,
         true,
         true,
         "State amplitudes: x = basis index (little-endian), y = Re, y_im = Im"},
        {{},
         "probabilities",
         "",
         "",
         FidelityClass::Exact,
         false,
         true,
         "Born probabilities per basis index"},
        {{},
         "reduced",
         "",
         "",
         FidelityClass::Exact,
         true,
         true,
         "Reduced density matrix of `qubits`: x = row·d + col"},
        {{}, "bloch_x", "", "", FidelityClass::Exact, false, true, "Tr(ρ_q X) per qubit"},
        {{}, "bloch_y", "", "", FidelityClass::Exact, false, true, "Tr(ρ_q Y) per qubit"},
        {{},
         "bloch_z",
         "",
         "",
         FidelityClass::Exact,
         false,
         true,
         "Tr(ρ_q Z) per qubit; |0> is +1"},
        {{},
         "purity",
         "",
         "",
         FidelityClass::Exact,
         false,
         true,
         "Tr ρ_q² per qubit; marker `global` = Tr ρ²"},
    });
}

namespace {
Result<std::array<double, 3>> blochOf(const qsim::Snapshot& s, std::uint32_t q) {
    const std::uint32_t one[] = {q};
    QXL_TRY_ASSIGN(num::Matrix rho, reducedDensity(s, one));
    return qsim::measures::blochVector(qubitBlock(rho));
}

double globalPurity(const qsim::Snapshot& s) {
    if (s.densityMatrix)
        return qsim::measures::purity(*s.densityMatrix);
    return 1.0; // a state vector is pure
}
} // namespace

std::optional<double> StateProbe::query(std::string_view path) const {
    auto run = runView();
    if (!run || !run->state)
        return std::nullopt;
    if (path == "purity")
        return globalPurity(*run->state);
    // qubit[i].bloch[k] / qubit[i].purity — consecutive scalar paths of spec 17 §5.
    if (!path.starts_with("qubit["))
        return InstrumentBase::query(path);
    const auto close = path.find(']');
    if (close == std::string_view::npos)
        return std::nullopt;
    std::uint32_t q = 0;
    if (std::from_chars(path.data() + 6, path.data() + close, q).ec != std::errc{})
        return std::nullopt;
    const std::string_view rest = path.substr(close + 1);
    if (rest == ".purity") {
        const std::uint32_t one[] = {q};
        auto rho = reducedDensity(*run->state, one);
        return rho ? std::optional(qsim::measures::purity(*rho)) : std::nullopt;
    }
    if (rest.starts_with(".bloch[") && rest.size() == 9 && rest[7] >= '0' && rest[7] <= '2') {
        auto r = blochOf(*run->state, q);
        return r ? std::optional((*r)[static_cast<std::size_t>(rest[7] - '0')]) : std::nullopt;
    }
    return std::nullopt;
}

Result<Trace> StateProbe::doAcquire(const ChannelDesc& channel, AcquireContext& ctx) {
    QXL_TRY_ASSIGN(auto state, stateOf(ctx, id()));
    const qsim::Snapshot& s = *state;
    Trace t = makeTrace(channel, ctx);
    t.cls =
        s.cls; // Exact for state-vector/density-matrix runs, Numerical for Lindblad (spec 12 §12)
    t.markers.push_back({0.0, 0.0, "gate_index", static_cast<double>(s.gateIndex), 0.0, ""});
    if (channel.name == "amplitudes") {
        if (!s.amplitudes)
            return fail(err::BadInput,
                        id().toString() + ": this backend exposes no amplitudes (use `reduced`)");
        for (std::size_t i = 0; i < s.amplitudes->size(); ++i) {
            t.x.push_back(static_cast<double>(i));
            t.y.push_back((*s.amplitudes)[i].real());
            t.y_im.push_back((*s.amplitudes)[i].imag());
        }
        return t;
    }
    if (channel.name == "probabilities") {
        if (s.probabilities)
            t.y = *s.probabilities;
        else if (s.amplitudes)
            for (auto const& a : *s.amplitudes)
                t.y.push_back(std::norm(a));
        else if (s.densityMatrix)
            for (std::size_t i = 0; i < s.densityMatrix->rows; ++i)
                t.y.push_back((*s.densityMatrix)(i, i).real());
        else
            return fail(err::BadInput, id().toString() + ": the snapshot holds no probabilities");
        for (std::size_t i = 0; i < t.y.size(); ++i)
            t.x.push_back(static_cast<double>(i));
        return t;
    }
    if (channel.name == "reduced") {
        auto qubits = parseQubitList(ctx.settings.text("qubits"), s.nQubits);
        if (!qubits)
            return fail(err::BadInput, id().toString() + ": " + qubits.error().message);
        QXL_TRY_ASSIGN(num::Matrix rho, reducedDensity(s, *qubits));
        for (std::size_t r = 0; r < rho.rows; ++r)
            for (std::size_t c = 0; c < rho.cols; ++c) {
                t.x.push_back(static_cast<double>(r * rho.cols + c));
                t.y.push_back(rho(r, c).real());
                t.y_im.push_back(rho(r, c).imag());
            }
        t.markers.push_back({0.0, 0.0, "dimension", static_cast<double>(rho.rows), 0.0, ""});
        t.markers.push_back({0.0, 0.0, "purity", qsim::measures::purity(rho), 0.0, ""});
        return t;
    }
    for (std::uint32_t q = 0; q < s.nQubits; ++q) {
        const std::uint32_t one[] = {q};
        QXL_TRY_ASSIGN(num::Matrix rho, reducedDensity(s, one));
        t.x.push_back(static_cast<double>(q));
        if (channel.name == "purity") {
            t.y.push_back(qsim::measures::purity(rho));
            continue;
        }
        QXL_TRY_ASSIGN(auto r, qsim::measures::blochVector(qubitBlock(rho)));
        t.y.push_back(r[channel.name == "bloch_x" ? 0 : channel.name == "bloch_y" ? 1 : 2]);
    }
    if (channel.name == "purity")
        t.markers.push_back({0.0, 0.0, "global", globalPurity(s), 0.0, ""});
    return t;
}

} // namespace qlab::instr
