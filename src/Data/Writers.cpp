#include "Data/Writers.hpp"
#include <charconv>
#include <cmath>
#include <fstream>

namespace qlab::data {

std::string formatDouble(double v) {
    if (std::isnan(v))
        return "nan";
    if (std::isinf(v))
        return v > 0 ? "inf" : "-inf";
    char buf[64];
    auto r = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::general, 17);
    std::string s(buf, r.ptr);
    // Trim to shortest representation that round-trips.
    for (int prec = 1; prec < 17; ++prec) {
        char b2[64];
        auto r2 = std::to_chars(b2, b2 + sizeof(b2), v, std::chars_format::general, prec);
        std::string s2(b2, r2.ptr);
        double back = 0;
        std::from_chars(s2.data(), s2.data() + s2.size(), back);
        if (back == v)
            return s2;
    }
    return s;
}

static std::string colHead(const std::string& n, const std::string& u) {
    return n + " (" + u + ")";
}

std::string csvHeader(std::span<const CsvColumn> cols) {
    std::string s;
    for (std::size_t i = 0; i < cols.size(); ++i) {
        if (i)
            s += ',';
        s += colHead(cols[i].name, cols[i].unit);
    }
    return s;
}

std::string toCsv(std::span<const CsvColumn> cols) {
    std::string out = csvHeader(cols) + "\n";
    std::size_t rows = 0;
    for (auto& c : cols)
        rows = std::max(rows, c.values.size());
    for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t i = 0; i < cols.size(); ++i) {
            if (i)
                out += ',';
            if (r < cols[i].values.size())
                out += formatDouble(cols[i].values[r]);
        }
        out += '\n';
    }
    return out;
}

Status writeCsv(const std::filesystem::path& path, std::span<const CsvColumn> cols) {
    return core::writeTextFileAtomic(path, toCsv(cols));
}

std::string traceToCsv(const Trace2D& t) {
    std::vector<CsvColumn> cols;
    cols.push_back({"x", t.xUnit, t.x});
    cols.push_back({t.yIm ? "y_re" : "y", t.yUnit, t.y});
    if (t.yIm)
        cols.push_back({"y_im", t.yUnit, *t.yIm});
    if (t.sigma)
        cols.push_back({"sigma", t.yUnit, *t.sigma});
    return toCsv(cols);
}
Status writeTraceCsv(const std::filesystem::path& path, const Trace2D& t) {
    return core::writeTextFileAtomic(path, traceToCsv(t));
}

std::string seriesToCsv(const Series& s) {
    std::vector<CsvColumn> cols;
    cols.push_back({"x", s.desc.xUnit, s.x});
    for (std::size_t k = 0; k < s.y.size(); ++k)
        cols.push_back({s.y.size() == 1 ? s.desc.id : s.desc.id + "[" + std::to_string(k) + "]",
                        s.desc.unit, s.y[k]});
    return toCsv(cols);
}

std::string sweep2dToCsv(std::span<const double> x, std::span<const double> y,
                         std::span<const double> z, const std::string& xu, const std::string& yu,
                         const std::string& zu) {
    std::string out = colHead("x", xu) + "," + colHead("y", yu) + "," + colHead("z", zu) + "\n";
    std::size_t n = std::min({x.size(), y.size(), z.size()});
    for (std::size_t i = 0; i < n; ++i)
        out += formatDouble(x[i]) + "," + formatDouble(y[i]) + "," + formatDouble(z[i]) + "\n";
    return out;
}

core::Json histogramToJson(const Histogram& h) {
    core::Json j;
    j["nbits"] = h.nbits();
    j["total"] = h.total();
    core::Json c = core::Json::object();
    for (auto& [k, v] : h.raw())
        c[k] = v;
    j["counts"] = c;
    if (h.theory)
        j["theory"] = *h.theory;
    j["class"] = fidelityName(h.cls);
    return j;
}
Result<Histogram> histogramFromJson(const core::Json& j) {
    if (!j.is_object() || !j.contains("counts"))
        return fail(ErrorCode::Parse, "histogram JSON needs counts");
    Histogram h(j.value("nbits", 0));
    for (auto& [k, v] : j["counts"].items())
        h.add(k, v.get<std::uint64_t>());
    if (j.contains("theory"))
        h.theory = j["theory"].get<std::vector<double>>();
    return h;
}
core::Json traceToJson(const Trace2D& t) {
    core::Json j;
    j["name"] = t.name;
    j["x_unit"] = t.xUnit;
    j["y_unit"] = t.yUnit;
    j["x"] = t.x;
    j["y"] = t.y;
    if (t.yIm)
        j["y_im"] = *t.yIm;
    if (t.sigma)
        j["sigma"] = *t.sigma;
    j["class"] = fidelityName(t.cls);
    j["timestamp_s"] = t.timestampS;
    j["markers"] = core::Json::array();
    for (auto& m : t.markers)
        j["markers"].push_back({{"x", m.x}, {"label", m.label}});
    return j;
}
Result<Trace2D> traceFromJson(const core::Json& j) {
    if (!j.is_object() || !j.contains("x") || !j.contains("y"))
        return fail(ErrorCode::Parse, "trace JSON needs x and y");
    Trace2D t;
    t.name = j.value("name", "");
    t.xUnit = j.value("x_unit", "");
    t.yUnit = j.value("y_unit", "");
    t.x = j["x"].get<std::vector<double>>();
    t.y = j["y"].get<std::vector<double>>();
    if (j.contains("y_im"))
        t.yIm = j["y_im"].get<std::vector<double>>();
    if (j.contains("sigma"))
        t.sigma = j["sigma"].get<std::vector<double>>();
    t.timestampS = j.value("timestamp_s", 0.0);
    if (j.contains("markers"))
        for (auto& m : j["markers"])
            t.markers.push_back({m.value("x", 0.0), m.value("label", "")});
    std::string c = j.value("class", "Numerical");
    t.cls = c == "Exact"          ? FidelityClass::Exact
            : c == "Statistical"  ? FidelityClass::Statistical
            : c == "Model"        ? FidelityClass::Model
            : c == "Illustrative" ? FidelityClass::Illustrative
                                  : FidelityClass::Numerical;
    return t;
}
} // namespace qlab::data
