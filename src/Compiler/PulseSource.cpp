// Spec 14 §6 — resolution of one gate invocation to its pulse block: program defcal first, then the
// device's pulses.json; QL4080 when neither calibrates the gate on those qubits.
#include "Compiler/PulseLower.hpp"
#include "Lang/Parser.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>

namespace qlab::compiler {

Result<pulse::ChannelId> channelOfPort(std::string_view port) {
    if (auto canonical = pulse::parseChannel(port)) return *canonical;   // "d[0]" style, when quoted upstream
    std::size_t split = 0;
    while (split < port.size() && std::isalpha(static_cast<unsigned char>(port[split]))) ++split;
    const std::string_view prefix = port.substr(0, split);
    std::vector<std::string> chunks;   // digit groups separated by '_'
    std::string digits;
    for (std::size_t i = split; i <= port.size(); ++i) {
        const char ch = i < port.size() ? port[i] : '_';
        if (std::isdigit(static_cast<unsigned char>(ch)) && digits.size() < 9) { digits += ch; continue; }
        if (ch != '_') return fail(err::BadCalibrationBlock, std::format("port '{}' does not name a channel", port));
        if (!digits.empty()) chunks.push_back(digits);
        digits.clear();
    }
    using K = pulse::ChannelKind;
    struct Kind { std::string_view prefix; K kind; };
    static constexpr Kind kinds[] = {{"d", K::Drive}, {"u", K::Control}, {"f", K::Flux}, {"m", K::Measure}, {"a", K::Acquire},
                                     {"acq", K::Acquire}, {"r", K::Raman}, {"ms", K::Bichromatic}};
    for (const Kind& k : kinds) {
        if (k.prefix != prefix) continue;
        const int arity = pulse::channelKindArity(k.kind);
        if (arity == 2 && chunks.size() == 1 && chunks[0].size() == 2) chunks = {chunks[0].substr(0, 1), chunks[0].substr(1, 1)};   // "u01"
        if (static_cast<int>(chunks.size()) != arity) break;
        const auto first = static_cast<std::uint32_t>(std::stoul(chunks[0]));
        return pulse::ChannelId{k.kind, first, arity == 2 ? static_cast<std::uint32_t>(std::stoul(chunks[1])) : 0u};
    }
    return fail(err::BadCalibrationBlock, std::format("port '{}' does not name a channel (expected d<k>, u<c>_<t>, f<k>, m<k>, a<k>, r<k> or ms<i>_<j>)", port));
}

Picoseconds PulseSource::quantise(double seconds, bool isPlay) const {
    return pulse::quantise(seconds, dt_, granularity_, isPlay ? minPulseSamples_ : 0);
}

Result<PulseSource> PulseSource::create(const hw::Device& device, const pulse::PulseLibrary* library, const ir::Circuit& program) {
    PulseSource s;
    s.device_ = &device;
    s.library_ = library;
    s.dt_ = Picoseconds{device.timing.dtPs};
    s.granularity_ = std::max(1, device.timing.granularitySamples);
    s.minPulseSamples_ = device.timing.minPulseSamples;
    const auto& meta = program.meta();
    if (!meta.contains("calibrations") || !meta["calibrations"].is_array() || meta["calibrations"].empty()) return s;

    // The calibrations travel as OpenQASM text (spec 13 §6): parse them again; no Sema is needed,
    // the program they came from was analysed already.
    std::string text = "OPENQASM 3.0;\ndefcalgrammar \"openpulse\";\n";
    for (const auto& block : meta["calibrations"])
        if (block.is_string()) text += block.get<std::string>() + "\n";
    lang::ParseResult parsed = lang::parse(text, "<calibrations>");
    if (lang::hasErrors(parsed.diagnostics)) {
        Error e(err::BadCalibrationBlock, "the program's calibration blocks do not parse");
        for (const auto& d : parsed.diagnostics)
            if (d.isError()) e.withNote(d.error.format());
        return fail(std::move(e));
    }
    s.ast_ = std::make_shared<lang::Ast>(std::move(parsed.ast));
    for (const auto& stmt : s.ast_->statements) {
        if (!stmt) continue;
        if (const auto* cal = stmt->as<lang::CalStmt>()) QXL_TRY(s.declare(cal->body, s.frames_, s.frameChannel_, s.waveforms_));
        if (const auto* d = stmt->as<lang::DefcalStmt>()) {
            ProgramDefcal pd;
            pd.gate = d->gate;
            pd.body = d;
            for (const auto& q : d->qubits) pd.qubits.push_back(q.index);
            for (std::size_t i = 0; i < d->params.size(); ++i) {
                const bool named = i < d->paramNames.size() && !d->paramNames[i].empty();
                pd.paramNames.push_back(named ? d->paramNames[i] : std::string());
                if (named) { pd.literals.emplace_back(); continue; }
                QXL_TRY_ASSIGN(const num::Complex v, s.evaluate(*d->params[i], {}));
                pd.literals.emplace_back(v.real());
            }
            s.defcals_.push_back(std::move(pd));
        }
    }
    return s;
}

const PulseSource::ProgramDefcal* PulseSource::find(std::string_view gate, std::span<const std::uint32_t> qubits,
                                                     std::span<const double> params) const {
    const ProgramDefcal* symbolic = nullptr;
    for (const ProgramDefcal& d : defcals_) {
        if (d.gate != gate || d.qubits.size() != qubits.size() || !std::equal(qubits.begin(), qubits.end(), d.qubits.begin())) continue;
        if (d.literals.size() != params.size()) continue;
        bool match = true, literal = false;
        for (std::size_t i = 0; i < params.size() && match; ++i)
            if (d.literals[i]) { literal = true; match = std::abs(*d.literals[i] - params[i]) < 1e-9; }
        if (!match) continue;
        if (literal) return &d;   // `defcal rz(pi/2) $0` beats `defcal rz(angle t) $0`
        if (!symbolic) symbolic = &d;
    }
    return symbolic;
}

bool PulseSource::hasProgramDefcal(std::string_view gate, std::span<const std::uint32_t> qubits) const {
    return std::any_of(defcals_.begin(), defcals_.end(), [&](const ProgramDefcal& d) {
        return d.gate == gate && d.qubits.size() == qubits.size() && std::equal(qubits.begin(), qubits.end(), d.qubits.begin());
    });
}

Error PulseSource::missing(std::string_view gate, std::span<const std::uint32_t> qubits, const SourceSpan& span) const {
    std::string where;
    for (std::size_t i = 0; i < qubits.size(); ++i) where += std::format("{}${}", i ? ", " : "", qubits[i]);
    return lang::Diagnostics::make("QL4080", span, gate, where).error;
}

Result<pulse::Schedule> PulseSource::fromLibrary(std::string_view gate, std::span<const std::uint32_t> qubits,
                                                 std::span<const double> params, const SourceSpan& span) const {
    const pulse::Defcal* d = library_ ? library_->find(gate, qubits) : nullptr;
    if (!d || d->params.size() > params.size()) return fail(missing(gate, qubits, span));
    pulse::ParamMap named;
    for (std::size_t i = 0; i < d->params.size(); ++i) named[d->params[i]] = params[i];
    auto block = library_->scheduleFor(gate, qubits, named);
    if (!block) return fail(block.error().withSpan(span));
    return block;
}

Result<pulse::Schedule> PulseSource::gateBlock(const ir::Gate& g) const {
    std::vector<std::uint32_t> qubits;
    for (ir::Wire w : g.wires()) qubits.push_back(w.index);
    if (const ProgramDefcal* d = find(g.name, qubits, g.params)) {
        auto block = lowerProgramDefcal(*d, g.params);
        if (!block) return fail(block.error().withSpan(g.span));
        return block;
    }
    if (g.opaque || !g.controls.empty() || g.adjoint || g.custom) return fail(missing(g.name, qubits, g.span));
    return fromLibrary(g.name, qubits, g.params, g.span);
}

Result<pulse::Schedule> PulseSource::measureBlock(std::uint32_t qubit, const SourceSpan& span) const {
    const std::uint32_t q[1] = {qubit};
    if (const ProgramDefcal* d = find("measure", q, {})) return lowerProgramDefcal(*d, {});
    return fromLibrary("measure", q, {}, span);
}

Result<pulse::Schedule> PulseSource::resetBlock(std::uint32_t qubit, const SourceSpan& span) const {
    const std::uint32_t q[1] = {qubit};
    if (const ProgramDefcal* d = find("reset", q, {})) return lowerProgramDefcal(*d, {});
    return fromLibrary("reset", q, {}, span);
}

} // namespace qlab::compiler
