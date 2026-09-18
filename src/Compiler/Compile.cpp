// Spec 14 §1, §11 — compile façade, pragma resolution, diagnostic formatting.
#include "Compiler/Compile.hpp"
#include "Pulse/Library.hpp"
#include <algorithm>
#include <format>

namespace qlab::compiler {

CompileContext resolveContext(const lang::Program& program, const CompileOptions& options, const hw::Device* device,
                              const hw::Calibration* calibration) {
    CompileContext c = CompileContext::from(options, device, calibration);
    const lang::Pragmas& pr = program.pragmas;
    if (!options.level) c.level = static_cast<OptimizeLevel>(std::clamp(pr.optimize, 0, 2));
    if (!options.layout && pr.layout) {
        switch (*pr.layout) {
        case lang::LayoutChoice::Trivial: c.layout = LayoutPolicy::Trivial; break;
        case lang::LayoutChoice::Dense: c.layout = LayoutPolicy::Dense; break;
        case lang::LayoutChoice::Vf2: c.layout = LayoutPolicy::Vf2; break;
        case lang::LayoutChoice::NoiseAware: c.layout = LayoutPolicy::NoiseAware; break;
        case lang::LayoutChoice::Physical: break;   // the circuit itself is physical: layout and routing only validate
        }
    }
    if (!options.routing) c.routing = pr.routing == lang::RoutingChoice::None ? RoutingPolicy::None : RoutingPolicy::Sabre;
    if (!options.pulseLevel && pr.pulseLevel) c.pulseLevel = *pr.pulseLevel;
    if (!options.seed && pr.seed) c.seed = *pr.seed;
    if (!device) c.pulseLevel = false;
    return c;
}

Result<CompiledProgram> compile(const lang::Program& program, const hw::Device& device, const hw::Calibration& calibration,
                                const CompileOptions& options, std::stop_token stop) {
    CompileContext ctx = resolveContext(program, options, &device, &calibration);
    std::optional<pulse::PulseLibrary> loaded;   // outlives the pipeline run below
    if (ctx.pulseLevel && !ctx.pulses) {
        if (device.directory.empty())
            return fail(err::NoDevice, std::format("device '{}' was not loaded from a directory: pass CompileOptions::pulses for a pulse-level compile", device.id));
        auto library = pulse::loadPulses(device.directory);
        if (!library) return fail(library.error().withNote(std::format("while loading pulses.json of device '{}'", device.id)));
        auto resolved = library->withCalibration(calibration);   // `cal.*` references follow the calibration in use
        loaded = resolved ? std::move(*resolved) : std::move(*library);
        ctx.pulses = &*loaded;
    }
    PassManager pm = PassManager::standard(ctx);
    return pm.run(program, options.inputs, std::move(stop));
}

Result<CompiledProgram> compile(const lang::Program& program, const CompileOptions& options, std::stop_token stop) {
    PassManager pm = PassManager::standard(resolveContext(program, options, nullptr, nullptr));
    return pm.run(program, options.inputs, std::move(stop));
}

Result<CompiledProgram> compileSource(std::string_view text, std::string filename, const hw::Device* device,
                                      const hw::Calibration* calibration, const CompileOptions& options, std::stop_token stop) {
    QXL_TRY_ASSIGN(const lang::Program program, lang::parseProgram(text, std::move(filename)));
    Result<CompiledProgram> out = device && calibration ? compile(program, *device, *calibration, options, std::move(stop))
                                                        : compile(program, options, std::move(stop));
    if (out) {
        out->programHash = programHash(text);
        // Front-end warnings (QL3xxx) come first, in source order.
        std::vector<lang::Diagnostic> all = program.diagnostics;
        all.insert(all.end(), out->diagnostics.begin(), out->diagnostics.end());
        out->diagnostics = std::move(all);
    }
    return out;
}

std::uint64_t programHash(std::string_view text) {
    std::uint64_t h = 0xCBF29CE484222325ull;
    for (unsigned char ch : text) {
        h ^= ch;
        h *= 0x100000001B3ull;
    }
    return h;
}

namespace {
std::string formatWith(const Error& e, std::string_view severity, std::string_view fix) {
    std::string s;
    if (e.span && e.span->line > 0)   // whole-program findings (QL4060, QL4090) carry no location
        s += std::format("{}:{}:{}: ", e.span->file.empty() ? "<program>" : e.span->file, e.span->line, e.span->column);
    s += severity;
    if (!e.diagnosticId.empty()) s += std::format("[{}]", e.diagnosticId);
    s += ": " + e.message;
    const std::string_view hint = e.diagnosticId.empty() ? std::string_view{} : lang::Diagnostics::info(e.diagnosticId).hint;
    for (const std::string& note : e.notes) {
        if (note.starts_with("help: ")) s += "\n  " + note;
        else if (!hint.empty() && note == hint) s += "\n  help: " + note;   // the catalogue's fix hint
        else s += "\n  note: " + note;
    }
    if (!fix.empty()) s += std::format("\n  help: replace with '{}'", fix);
    return s;
}
} // namespace

std::string formatDiagnostic(const lang::Diagnostic& d) {
    const char* severity = d.severity == lang::Severity::Error ? "error" : d.severity == lang::Severity::Warning ? "warning" : "info";
    return formatWith(d.error, severity, d.fix);
}

std::string formatError(const Error& e) { return formatWith(e, "error", {}); }

} // namespace qlab::compiler
