#include "Data/Recipe.hpp"
#include "Data/Models.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace qlab::data {
namespace {
constexpr const char* kKind = "analysis.recipe"; // matches Assets/Analysis/*.json and spec 22 §6
struct Registrar {
    Registrar() { core::JsonEnvelope::registerKind(kKind, 1); }
} registrar;

// Fit models the recipes may name. `fit::makeModel` covers the curve fits; these two are
// maximum-likelihood tomography reconstructions implemented in `qsim::tomo` (spec 22 §4), and
// `gmm2` is the IQ-cloud mixture, which has its own entry point.
bool knownFitModel(const std::string& m) {
    static const std::set<std::string> extra{"gmm2", "mle_state", "mle_process"};
    return extra.contains(m) || static_cast<bool>(fit::makeModel(m));
}

std::vector<double> expandValues(const core::Json& v, SweepAxis& axis) {
    std::vector<double> out;
    if (v.is_array()) {
        axis.scale = "list";
        for (const auto& x : v)
            if (x.is_number())
                out.push_back(x.get<double>());
        return out;
    }
    if (!v.is_object())
        return out;
    for (const char* mode : {"linear", "log"}) {
        if (!v.contains(mode) || !v[mode].is_array() || v[mode].size() != 3)
            continue;
        axis.scale = mode;
        axis.from = v[mode][0].get<double>();
        axis.to = v[mode][1].get<double>();
        axis.points = v[mode][2].get<int>();
        if (axis.points <= 0)
            return out;
        if (axis.points == 1)
            return {axis.from};
        out.reserve(static_cast<std::size_t>(axis.points));
        const bool logScale = axis.scale == "log" && axis.from > 0 && axis.to > 0;
        for (int i = 0; i < axis.points; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(axis.points - 1);
            out.push_back(logScale ? axis.from * std::pow(axis.to / axis.from, t)
                                   : axis.from + (axis.to - axis.from) * t);
        }
        return out;
    }
    return out;
}

std::optional<SweepAxis> readAxis(const core::Json& s, const char* inputKey, const char* unitKey,
                                  const char* valuesKey, const char* labelsKey) {
    if (!s.contains(inputKey))
        return std::nullopt;
    SweepAxis a;
    a.input = s.value(inputKey, "");
    a.unit = s.value(unitKey, "");
    if (s.contains(valuesKey))
        a.values = expandValues(s[valuesKey], a);
    if (s.contains(labelsKey) && s[labelsKey].is_array())
        for (const auto& l : s[labelsKey])
            if (l.is_string())
                a.labels.push_back(l.get<std::string>());
    return a;
}

core::Json axisToJson(const SweepAxis& a, core::Json s, const char* inputKey, const char* unitKey,
                      const char* valuesKey, const char* labelsKey) {
    s[inputKey] = a.input;
    s[unitKey] = a.unit;
    if (a.scale == "linear" || a.scale == "log")
        s[valuesKey] = core::Json{{a.scale, core::Json::array({a.from, a.to, a.points})}};
    else
        s[valuesKey] = a.values;
    if (!a.labels.empty())
        s[labelsKey] = a.labels;
    return s;
}
} // namespace

Result<Recipe> recipeFromJson(const core::Json& d) {
    if (!d.is_object())
        return fail(ErrorCode::Data_ + 30, "recipe data must be an object");
    Recipe r;
    r.id = d.value("id", "");
    r.title = d.value("title", r.id);
    r.theory = d.value("theory", "");
    r.description = d.value("description", "");
    r.program = d.value("program", "");
    r.shots = d.value("shots", 1024);

    if (d.contains("sweep") && d["sweep"].is_object()) {
        const core::Json& s = d["sweep"];
        r.sweep = readAxis(s, "input", "unit", "values", "labels");
        r.sweep2 = readAxis(s, "input2", "unit2", "values2", "labels2");
    }
    if (d.contains("extract") && d["extract"].is_object()) {
        const core::Json& e = d["extract"];
        r.extract.channel = e.value("channel", "");
        r.extract.y = e.value("y", "");
        r.extract.sigma = e.value("sigma", "none");
        r.extract.sequences = e.value("sequences", 0);
        r.extract.averageOver = e.value("average_over", "");
        r.extract.extra = core::Json::object();
        for (const auto& [k, v] : e.items())
            if (k != "channel" && k != "y" && k != "sigma" && k != "sequences" &&
                k != "average_over")
                r.extract.extra[k] = v;
    }
    if (d.contains("fit") && d["fit"].is_object()) {
        // "none" is how a recipe says it has no curve fit (a 2D map is read, not fitted).
        r.fitModel = d["fit"].value("model", "");
        if (r.fitModel == "none")
            r.fitModel.clear();
        if (d["fit"].contains("report") && d["fit"]["report"].is_array())
            for (const auto& f : d["fit"]["report"])
                if (f.is_string())
                    r.report.push_back(f.get<std::string>());
    }
    if (d.contains("results") && d["results"].is_array())
        for (const auto& row : d["results"])
            r.results.push_back({row.value("label", ""), row.value("measured", ""),
                                 row.value("model", ""), row.value("tolerance", 0.0)});
    if (d.contains("generator") && d["generator"].is_object()) {
        GeneratorSpec g;
        g.kind = d["generator"].value("kind", "");
        g.params = d["generator"];
        g.params.erase("kind");
        r.generator = std::move(g);
    }
    // Unknown top-level keys are preserved (spec 23 §1 forward compatibility).
    r.extra = core::Json::object();
    static const std::set<std::string> known{"id",      "title",   "theory",   "description",
                                             "program", "sweep",   "shots",    "extract",
                                             "fit",     "results", "generator"};
    for (const auto& [k, v] : d.items())
        if (!known.contains(k))
            r.extra[k] = v;

    QXL_TRY(validateRecipe(r));
    return r;
}

core::Json recipeToJson(const Recipe& r) {
    core::Json d = core::Json::object();
    d["id"] = r.id;
    d["title"] = r.title;
    if (!r.theory.empty())
        d["theory"] = r.theory;
    if (!r.description.empty())
        d["description"] = r.description;
    if (!r.program.empty())
        d["program"] = r.program;
    if (r.sweep) {
        core::Json s =
            axisToJson(*r.sweep, core::Json::object(), "input", "unit", "values", "labels");
        if (r.sweep2)
            s = axisToJson(*r.sweep2, std::move(s), "input2", "unit2", "values2", "labels2");
        d["sweep"] = std::move(s);
    }
    d["shots"] = r.shots;
    core::Json e = r.extract.extra.is_object() ? r.extract.extra : core::Json::object();
    e["channel"] = r.extract.channel;
    e["y"] = r.extract.y;
    e["sigma"] = r.extract.sigma;
    if (r.extract.sequences > 0)
        e["sequences"] = r.extract.sequences;
    if (!r.extract.averageOver.empty())
        e["average_over"] = r.extract.averageOver;
    d["extract"] = std::move(e);
    if (!r.fitModel.empty()) {
        core::Json f;
        f["model"] = r.fitModel;
        f["report"] = r.report;
        d["fit"] = std::move(f);
    }
    if (!r.results.empty()) {
        d["results"] = core::Json::array();
        for (const auto& row : r.results)
            d["results"].push_back({{"label", row.label},
                                    {"measured", row.measured},
                                    {"model", row.model},
                                    {"tolerance", row.tolerance}});
    }
    if (r.generator) {
        core::Json g = r.generator->params.is_object() ? r.generator->params : core::Json::object();
        g["kind"] = r.generator->kind;
        d["generator"] = std::move(g);
    }
    if (r.extra.is_object())
        for (const auto& [k, v] : r.extra.items())
            d[k] = v;
    return d;
}

Status validateRecipe(const Recipe& r) {
    if (r.id.empty())
        return fail(ErrorCode::Data_ + 31, "recipe id is empty");
    // Spec 22 §6: a recipe names a program, or declares the generator that builds the family.
    if (r.program.empty() && !r.generator)
        return fail(ErrorCode::Data_ + 32,
                    "recipe '" + r.id + "' has neither a program nor a generator");
    if (!r.program.empty() && r.generator)
        return fail(ErrorCode::Data_ + 32,
                    "recipe '" + r.id + "' names both a program and a generator");
    if (r.generator && r.generator->kind.empty())
        return fail(ErrorCode::Data_ + 33, "recipe '" + r.id + "' has a generator with no kind");
    for (const SweepAxis* a : {r.sweep ? &*r.sweep : nullptr, r.sweep2 ? &*r.sweep2 : nullptr}) {
        if (a == nullptr)
            continue;
        if (a->input.empty())
            return fail(ErrorCode::Data_ + 34, "recipe '" + r.id + "': sweep axis has no input");
        if (a->values.empty())
            return fail(ErrorCode::Data_ + 35,
                        "recipe '" + r.id + "': sweep axis '" + a->input + "' has no points");
        if (!a->labels.empty() && a->labels.size() != a->values.size())
            return fail(ErrorCode::Data_ + 36,
                        "recipe '" + r.id + "': sweep labels do not match the value count");
    }
    if (!r.fitModel.empty() && !knownFitModel(r.fitModel))
        return fail(ErrorCode::Data_ + 37,
                    "recipe '" + r.id + "': unknown fit model '" + r.fitModel + "'");
    if (r.shots < 1)
        return fail(ErrorCode::Data_ + 38, "recipe '" + r.id + "': shots must be >= 1");
    return {};
}

Result<Recipe> loadRecipe(const std::filesystem::path& path) {
    auto e = core::JsonEnvelope::load(path, kKind);
    if (!e)
        return std::unexpected(e.error());
    auto r = recipeFromJson(e->data);
    if (!r)
        r.error().notes.push_back("file: " + path.string());
    return r;
}
Status saveRecipe(const std::filesystem::path& path, const Recipe& r) {
    QXL_TRY(validateRecipe(r));
    return core::JsonEnvelope::save(path, kKind, recipeToJson(r));
}
Result<std::vector<Recipe>> loadRecipeDirectory(const std::filesystem::path& dir) {
    std::vector<Recipe> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec))
        return fail(ErrorCode::NotFound, "no recipe directory " + dir.string());
    std::vector<std::filesystem::path> files;
    for (auto& e : std::filesystem::directory_iterator(dir))
        if (e.path().extension() == ".json")
            files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (auto& f : files) {
        auto r = loadRecipe(f);
        if (!r)
            return std::unexpected(r.error());
        out.push_back(std::move(*r));
    }
    return out;
}
} // namespace qlab::data
