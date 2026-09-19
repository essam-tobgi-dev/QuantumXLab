#include "Report/ProgramExport.hpp"

#include "Data/Writers.hpp"
#include <format>
#include <map>

namespace qlab::report {

std::string estimatePragma(const runtime::Estimate& e) {
    // Spec 23 §5: `pragma qlab.estimate wall_s=4.21 fidelity=0.91`. `lang::applyPragma` accepts the
    // line on re-import (spec 13 §7), so the compiled export still parses.
    return std::format("pragma qlab.estimate wall_s={:.6g} fidelity={:.6g}", e.wallTime.valueS,
                       e.fidelity.fast);
}

Result<std::string> compiledQasm(const compiler::CompiledProgram& program,
                                 const ProgramExportOptions& options) {
    compiler::ExportOptions eo;
    eo.explicitDelays = options.explicitDelays;
    eo.header = options.header;
    QXL_TRY_ASSIGN(std::string text, compiler::exportQasm(program, eo));
    if (!options.estimate)
        return text;

    // The estimate pragma joins the pragma block the exporter wrote: after the last `pragma qlab.`
    // line when there is one, else directly after the `include` line (spec 23 §5 header order).
    std::size_t insert = std::string::npos;
    for (std::size_t at = text.find("pragma qlab."); at != std::string::npos;
         at = text.find("pragma qlab.", at + 1)) {
        const std::size_t eol = text.find('\n', at);
        if (eol == std::string::npos)
            break;
        insert = eol + 1;
    }
    if (insert == std::string::npos) {
        const std::size_t firstLine = text.find('\n');
        const std::size_t secondLine =
            firstLine == std::string::npos ? std::string::npos : text.find('\n', firstLine + 1);
        if (secondLine == std::string::npos)
            return fail(ErrorCode::Internal, "unexpected OpenQASM preamble");
        insert = secondLine + 1;
    }
    text.insert(insert, estimatePragma(*options.estimate) + "\n");
    return text;
}

Status writeCompiledQasm(const std::filesystem::path& path,
                         const compiler::CompiledProgram& program,
                         const ProgramExportOptions& options) {
    QXL_TRY_ASSIGN(const std::string text, compiledQasm(program, options));
    return core::writeTextFileAtomic(path, text);
}

Status writeProgramSource(const std::filesystem::path& path, std::string_view source) {
    return core::writeTextFileAtomic(path, source);
}

std::string scheduleSamplesCsv(const pulse::Schedule& schedule) {
    const std::int64_t dtPs = schedule.dt().value > 0 ? schedule.dt().value : 222;
    const std::int64_t totalPs = schedule.duration().value;
    const std::size_t n =
        totalPs > 0 ? static_cast<std::size_t>((totalPs + dtPs - 1) / dtPs) + 1 : 0;

    std::vector<pulse::ChannelId> channels;
    for (auto ch : schedule.channels())
        if (ch.isDrivelike())
            channels.push_back(ch);

    // Zero-order-hold accumulation of every `Play` on its channel (spec 10 §1 sample grid).
    std::map<pulse::ChannelId, std::vector<num::Complex>> waves;
    for (auto ch : channels)
        waves[ch].assign(n, num::Complex{0.0, 0.0});
    for (const auto& instr : schedule.instructions()) {
        const auto* play = std::get_if<pulse::Play>(&instr);
        if (!play || !play->ch.isDrivelike())
            continue;
        auto it = waves.find(play->ch);
        if (it == waves.end())
            continue;
        const std::vector<num::Complex> s = play->wf.sampled(dtPs);
        const std::size_t first = static_cast<std::size_t>(play->t0.value / dtPs);
        for (std::size_t k = 0; k < s.size() && first + k < n; ++k)
            it->second[first + k] += s[k];
    }

    std::vector<double> time(n);
    for (std::size_t i = 0; i < n; ++i)
        time[i] = static_cast<double>(static_cast<std::int64_t>(i) * dtPs) * 1e-12;
    std::vector<std::vector<double>> parts;
    parts.reserve(channels.size() * 2);
    std::vector<data::CsvColumn> cols;
    cols.push_back({"t", "s", time});
    for (auto ch : channels) {
        const auto& w = waves[ch];
        std::vector<double> re(n), im(n);
        for (std::size_t i = 0; i < n; ++i) {
            re[i] = w[i].real();
            im[i] = w[i].imag();
        }
        parts.push_back(std::move(re));
        parts.push_back(std::move(im));
    }
    for (std::size_t c = 0; c < channels.size(); ++c) {
        cols.push_back({channels[c].toString() + "_re", "arb", parts[2 * c]});
        cols.push_back({channels[c].toString() + "_im", "arb", parts[2 * c + 1]});
    }
    return data::toCsv(cols);
}

Status writeScheduleSamplesCsv(const std::filesystem::path& path, const pulse::Schedule& schedule) {
    return core::writeTextFileAtomic(path, scheduleSamplesCsv(schedule));
}

} // namespace qlab::report
