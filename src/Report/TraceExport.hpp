#pragma once
// Spec 23 §7 — instrument traces and channels as CSV. The provenance (fidelity class, instrument
// id, settings) is written as `#`-prefixed comment lines before the header, so the file stays a
// valid CSV for spreadsheet tools while carrying where its numbers came from.
//
// Headers are `name (unit)` (spec 22 §7, `data::toCsv`), one line, not the two-line form of the
// §7 sketch — see SPEC_DEVIATIONS 57.
#include "Data/Series.hpp"
#include "Data/Fidelity.hpp"
#include "Report/Types.hpp"
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace qlab::report {

struct TraceProvenance {
    std::string instrument;      // instrument id, "vna", "sa", "digitizer"
    std::string timestamp;       // ISO 8601 UTC; empty = now
    // Instrument settings in display order: span, RBW, averaging, sample rate, …
    std::vector<std::pair<std::string, std::string>> settings;
    std::vector<std::string> notes;
};

// Comment block only (each line starts with '#', the block ends with a newline).
std::string provenanceComments(const TraceProvenance& p, data::FidelityClass cls, std::string_view name);

std::string traceToCsv(const data::Trace2D& t, const TraceProvenance& p = {});
Status writeTraceCsv(const std::filesystem::path& path, const data::Trace2D& t, const TraceProvenance& p = {});
std::string seriesToCsv(const data::Series& s, const TraceProvenance& p = {});
Status writeSeriesCsv(const std::filesystem::path& path, const data::Series& s, const TraceProvenance& p = {});

} // namespace qlab::report
