#include "Report/TraceExport.hpp"
#include "Data/Fidelity.hpp"

#include "Core/Version.hpp"
#include "Data/Writers.hpp"

namespace qlab::report {

std::string provenanceComments(const TraceProvenance& p, data::FidelityClass cls, std::string_view name) {
    std::string out;
    if (!name.empty()) out += "# trace: " + std::string(name) + "\n";
    out += "# class: " + std::string(data::fidelityName(cls)) + "\n";
    if (!p.instrument.empty()) out += "# instrument: " + p.instrument + "\n";
    out += "# time: " + (p.timestamp.empty() ? core::isoNow() : p.timestamp) + "\n";
    out += "# app: " + std::string(core::version()) + "\n";
    for (const auto& [k, v] : p.settings) out += "# " + k + ": " + v + "\n";
    for (const auto& n : p.notes) out += "# note: " + n + "\n";
    return out;
}

std::string traceToCsv(const data::Trace2D& t, const TraceProvenance& p) {
    return provenanceComments(p, t.cls, t.name) + data::traceToCsv(t);
}

Status writeTraceCsv(const std::filesystem::path& path, const data::Trace2D& t, const TraceProvenance& p) {
    return core::writeTextFileAtomic(path, traceToCsv(t, p));
}

std::string seriesToCsv(const data::Series& s, const TraceProvenance& p) {
    return provenanceComments(p, s.desc.cls, s.desc.id) + data::seriesToCsv(s);
}

Status writeSeriesCsv(const std::filesystem::path& path, const data::Series& s, const TraceProvenance& p) {
    return core::writeTextFileAtomic(path, seriesToCsv(s, p));
}

} // namespace qlab::report
