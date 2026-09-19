#pragma once
// Spec 22 §7 / 23 §5 — CSV and JSON writers. Locale-independent, 17 significant digits,
// units in column headers as "name (unit)".
#include "Core/Error.hpp"
#include "Core/Json.hpp"
#include "Data/Histogram.hpp"
#include "Data/Series.hpp"
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace qlab::data {

struct CsvColumn {
    std::string name;
    std::string unit;
    std::span<const double> values;
};

std::string formatDouble(double v); // shortest round-trip, '.' decimal, "nan"/"inf"
std::string csvHeader(std::span<const CsvColumn> cols); // "t (us),p1 ()"
std::string toCsv(std::span<const CsvColumn> cols); // header + rows; ragged columns padded with ""
Status writeCsv(const std::filesystem::path& path, std::span<const CsvColumn> cols);
std::string traceToCsv(const Trace2D& t);
Status writeTraceCsv(const std::filesystem::path& path, const Trace2D& t);
std::string seriesToCsv(const Series& s);
// Long format for sweeps: x, y, z columns.
std::string sweep2dToCsv(std::span<const double> x, std::span<const double> y,
                         std::span<const double> z, const std::string& xu, const std::string& yu,
                         const std::string& zu);

core::Json
histogramToJson(const Histogram& h); // {"nbits", "total", "counts": {label: n}, "theory"?}
Result<Histogram> histogramFromJson(const core::Json& j);
core::Json traceToJson(const Trace2D& t);
Result<Trace2D> traceFromJson(const core::Json& j);

} // namespace qlab::data
