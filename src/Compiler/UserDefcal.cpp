// Spec 13 §6, 14 §6 — lowering of a program `defcal` body onto `pulse::Schedule`: ports, frames,
// waveforms, play, frame operations, delays, captures and barriers. Expressions are evaluated with
// the defcal's formal parameters bound to the gate's arguments; durations land on the device grid.
#include "Compiler/PulseLower.hpp"
#include <cmath>
#include <format>
#include <numbers>

namespace qlab::compiler {
namespace {
template <class... Ts> struct Overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

Error bad(std::string message, const SourceSpan& span) { return Error(err::BadCalibrationBlock, std::move(message)).withSpan(span); }
} // namespace

Result<num::Complex> PulseSource::evaluate(const lang::Expr& e, const Bindings& params) const {
    using namespace lang;
    using C = num::Complex;
    if (const auto* n = e.as<IntLit>()) return C(static_cast<double>(n->value));
    if (const auto* n = e.as<FloatLit>()) return C(n->value);
    if (const auto* n = e.as<ImagLit>()) return C(0.0, n->value);
    if (const auto* n = e.as<BoolLit>()) return C(n->value ? 1.0 : 0.0);
    if (const auto* n = e.as<DurationLit>()) {   // seconds
        switch (n->unit) {
        case DurationUnit::Ns: return C(n->value * 1e-9);
        case DurationUnit::Us: return C(n->value * 1e-6);
        case DurationUnit::Ms: return C(n->value * 1e-3);
        case DurationUnit::S: return C(n->value);
        case DurationUnit::Dt: return C(n->value * static_cast<double>(dt_.get()) * 1e-12);
        case DurationUnit::None: break;
        }
        return fail(bad("duration without a unit in a calibration block", e.span));
    }
    if (const auto* n = e.as<ConstantRef>()) {
        if (n->name == "pi" || n->name == "π") return C(std::numbers::pi);
        if (n->name == "tau" || n->name == "τ") return C(2.0 * std::numbers::pi);
        if (n->name == "euler" || n->name == "ℇ") return C(std::numbers::e);
        return fail(bad(std::format("unknown constant '{}'", n->name), e.span));
    }
    if (const auto* n = e.as<Ident>()) {
        if (auto it = params.find(n->name); it != params.end()) return C(it->second);
        return fail(bad(std::format("'{}' has no value inside the calibration block", n->name), e.span));
    }
    if (const auto* n = e.as<UnaryExpr>()) {
        if (n->op != UnaryOp::Neg || !n->operand) return fail(bad("unsupported unary operator in a calibration block", e.span));
        QXL_TRY_ASSIGN(const C v, evaluate(*n->operand, params));
        return -v;
    }
    if (const auto* n = e.as<BinaryExpr>()) {
        if (!n->lhs || !n->rhs) return fail(bad("malformed expression in a calibration block", e.span));
        QXL_TRY_ASSIGN(const C a, evaluate(*n->lhs, params));
        QXL_TRY_ASSIGN(const C b, evaluate(*n->rhs, params));
        switch (n->op) {
        case BinaryOp::Add: return a + b;
        case BinaryOp::Sub: return a - b;
        case BinaryOp::Mul: return a * b;
        case BinaryOp::Div: return a / b;
        case BinaryOp::Pow: return std::pow(a, b);
        default: return fail(bad(std::format("operator '{}' is not available in a calibration block", binaryOpName(n->op)), e.span));
        }
    }
    if (const auto* n = e.as<CallExpr>()) {
        if (n->args.size() != 1 || !n->args[0]) return fail(bad(std::format("'{}' takes one argument", n->callee), e.span));
        QXL_TRY_ASSIGN(const C v, evaluate(*n->args[0], params));
        if (n->callee == "sin") return std::sin(v);
        if (n->callee == "cos") return std::cos(v);
        if (n->callee == "tan") return std::tan(v);
        if (n->callee == "exp") return std::exp(v);
        if (n->callee == "sqrt") return std::sqrt(v);
        if (n->callee == "ln" || n->callee == "log") return std::log(v);
        return fail(bad(std::format("function '{}' is not available in a calibration block", n->callee), e.span));
    }
    if (const auto* n = e.as<CastExpr>(); n && n->operand) return evaluate(*n->operand, params);
    return fail(bad("expression cannot be evaluated in a calibration block", e.span));
}

Status PulseSource::declare(const std::vector<lang::PulseStmt>& body, std::vector<pulse::FrameDecl>& frames, ChannelMap& channels,
                            WaveformMap& waveforms) const {
    for (const lang::PulseStmt& ps : body) {
        if (const auto* f = std::get_if<lang::PulseFrameDecl>(&ps.node)) {
            auto ch = channelOfPort(f->port);
            if (!ch) return fail(ch.error().withSpan(ps.span));
            pulse::FrameDecl decl;
            decl.name = f->name;
            decl.channel = *ch;
            if (f->frequency) { QXL_TRY_ASSIGN(const num::Complex hz, evaluate(*f->frequency, {})); decl.frequencyHz = hz.real(); }
            if (f->phase) { QXL_TRY_ASSIGN(const num::Complex ph, evaluate(*f->phase, {})); decl.phase = ph.real(); }
            channels[f->name] = *ch;
            frames.push_back(std::move(decl));
        } else if (const auto* w = std::get_if<lang::PulseWaveformDecl>(&ps.node)) {
            waveforms[w->name] = &w->waveform;
        }
    }
    return {};
}

Result<pulse::Waveform> PulseSource::waveform(const lang::PulseWaveform& w, const Bindings& params, const WaveformMap& local) const {
    const double dtSeconds = static_cast<double>(dt_.get()) * 1e-12;
    if (w.kind == "ref") {
        const auto* id = w.args.empty() || !w.args[0] ? nullptr : w.args[0]->as<lang::Ident>();
        if (!id) return fail(err::BadCalibrationBlock, "malformed waveform reference");
        auto it = local.find(id->name);
        if (it == local.end()) { it = waveforms_.find(id->name); if (it == waveforms_.end()) return fail(err::BadCalibrationBlock, std::format("waveform '{}' is not declared", id->name)); }
        return waveform(*it->second, params, local);
    }
    std::vector<num::Complex> a;
    for (const auto& arg : w.args) {
        if (!arg) return fail(err::BadCalibrationBlock, "malformed waveform argument");
        QXL_TRY_ASSIGN(const num::Complex v, evaluate(*arg, params));
        a.push_back(v);
    }
    if (w.kind == "samples") return pulse::Waveform::fromSamples(std::move(a), dtSeconds);
    const std::size_t want = w.kind == "constant" ? 2 : (w.kind == "gaussian" || w.kind == "sech" || w.kind == "cosine") ? 3 : 4;
    if (a.size() != want) return fail(err::BadCalibrationBlock, std::format("waveform '{}' takes {} arguments, got {}", w.kind, want, a.size()));
    // A real amplitude keeps its sign (echoed CR halves); a complex one becomes magnitude and phase.
    const bool real = std::abs(a[0].imag()) < 1e-15;
    const double amp = real ? a[0].real() : std::abs(a[0]), phase = real ? 0.0 : std::arg(a[0]);
    const double T = pulse::secondsOf(quantise(a[1].real(), true));
    pulse::Waveform out;
    if (w.kind == "gaussian") out = pulse::Waveform::gaussian(T, a[2].real(), amp, phase);
    else if (w.kind == "gaussian_square") {   // (amp, duration, square width, sigma): the rest of the duration is the two edges
        const double rise = std::max(0.0, (T - a[2].real()) / 2.0);
        out = pulse::Waveform::gaussianSquare(T, a[3].real(), rise, amp, phase);
    } else if (w.kind == "drag") {
        // β of the shipped programs and of pulses.json (`beta_ns`) is in nanoseconds: the optimum
        // −1/(2πα) of a transmon is ≈ 0.5 ns, inside the ±1 range the examples sweep.
        out = pulse::Waveform::drag(T, a[2].real(), a[3].real() * 1e-9, amp, phase);
    } else if (w.kind == "constant") out = pulse::Waveform::constant(T, amp, phase);
    else if (w.kind == "sech") out = pulse::Waveform::sech(T, a[2].real(), amp, phase);
    else if (w.kind == "cosine") out = pulse::Waveform::cosine(T, amp, a[2].real());
    else if (w.kind == "sine") {   // (amp, duration, frequency, phase): sampled at dt
        std::vector<num::Complex> samples(static_cast<std::size_t>(std::llround(T / dtSeconds)));
        for (std::size_t k = 0; k < samples.size(); ++k)
            samples[k] = a[0] * std::sin(2.0 * std::numbers::pi * a[2].real() * (static_cast<double>(k) + 0.5) * dtSeconds + a[3].real());
        out = pulse::Waveform::fromSamples(std::move(samples), dtSeconds);
    } else return fail(err::BadCalibrationBlock, std::format("unknown waveform '{}'", w.kind));
    QXL_TRY(out.validate());
    return out;
}

Result<pulse::Schedule> PulseSource::lowerProgramDefcal(const ProgramDefcal& d, std::span<const double> params) const {
    Bindings bound;
    for (std::size_t i = 0; i < d.paramNames.size() && i < params.size(); ++i)
        if (!d.paramNames[i].empty()) bound[d.paramNames[i]] = params[i];
    std::vector<pulse::FrameDecl> localFrames;
    ChannelMap localChannels;
    WaveformMap localWaveforms;
    QXL_TRY(declare(d.body->body, localFrames, localChannels, localWaveforms));   // a frame made in a defcal is local to it
    auto channel = [&](const std::string& frame, const SourceSpan& span) -> Result<pulse::ChannelId> {
        if (auto it = localChannels.find(frame); it != localChannels.end()) return it->second;
        if (auto it = frameChannel_.find(frame); it != frameChannel_.end()) return it->second;
        return fail(bad(std::format("frame '{}' is not declared in a cal block", frame), span));
    };
    pulse::Schedule s(dt_);
    for (const lang::PulseStmt& ps : d.body->body) {
        Status st = std::visit(
            Overloaded{
                [&](const lang::PulsePlay& p) -> Status {
                    QXL_TRY_ASSIGN(const pulse::ChannelId ch, channel(p.frame, ps.span));
                    const lang::PulseWaveform* wf = std::get_if<lang::PulseWaveform>(&p.waveform);
                    if (const auto* name = std::get_if<std::string>(&p.waveform)) {   // `play(frame, w)` of a declared waveform
                        if (auto it = localWaveforms.find(*name); it != localWaveforms.end()) wf = it->second;
                        else if (auto global = waveforms_.find(*name); global != waveforms_.end()) wf = global->second;
                        else return fail(bad(std::format("waveform '{}' is not declared", *name), ps.span));
                    }
                    if (!wf) return fail(bad("play without a waveform", ps.span));
                    Result<pulse::Waveform> made = waveform(*wf, bound, localWaveforms);
                    if (!made) return fail(made.error().withSpan(ps.span));
                    s.append(pulse::Play{ch, std::move(*made), {}});
                    return {};
                },
                [&](const lang::PulseFrameOp& f) -> Status {
                    QXL_TRY_ASSIGN(const pulse::ChannelId ch, channel(f.frame, ps.span));
                    QXL_TRY_ASSIGN(const num::Complex v, evaluate(*f.value, bound));
                    using Op = pulse::FrameOp::Op;
                    const Op op = f.op == "set_frequency" ? Op::SetFrequency : f.op == "shift_frequency" ? Op::ShiftFrequency
                                  : f.op == "set_phase"   ? Op::SetPhase     : Op::ShiftPhase;
                    s.append(pulse::FrameOp{ch, {}, op, v.real()});
                    return {};
                },
                [&](const lang::PulseDelay& dl) -> Status {
                    QXL_TRY_ASSIGN(const num::Complex v, evaluate(*dl.duration, bound));
                    for (const std::string& frame : dl.frames) {
                        QXL_TRY_ASSIGN(const pulse::ChannelId ch, channel(frame, ps.span));
                        s.append(pulse::Delay{ch, {}, quantise(v.real(), false)});
                    }
                    return {};
                },
                [&](const lang::PulseCapture& c) -> Status {
                    QXL_TRY_ASSIGN(const pulse::ChannelId ch, channel(c.frame, ps.span));
                    QXL_TRY_ASSIGN(const num::Complex v, evaluate(*c.duration, bound));
                    pulse::Acquire acquire;
                    acquire.ch = ch;
                    acquire.length = quantise(v.real(), false);
                    s.append(std::move(acquire));
                    return {};
                },
                [&](const lang::PulseBarrier& b) -> Status {
                    std::vector<pulse::ChannelId> chs;
                    for (const std::string& frame : b.frames) {
                        QXL_TRY_ASSIGN(const pulse::ChannelId ch, channel(frame, ps.span));
                        chs.push_back(ch);
                    }
                    s.barrier(std::move(chs));
                    return {};
                },
                [](const auto&) -> Status { return {}; },   // declarations were registered by `declare`
            },
            ps.node);
        QXL_TRY(st);
    }
    for (const pulse::FrameDecl& f : localFrames) s.addFrame(f);
    return s;
}

} // namespace qlab::compiler
