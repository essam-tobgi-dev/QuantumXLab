// Spec 17 §4 — component descriptor loading and lint.
#include "Lab/Catalog.hpp"
#include "Core/Paths.hpp"
#include "Data/Fidelity.hpp"
#include "Lab/Binding.hpp"
#include <algorithm>
#include <format>
#include <sstream>

namespace qlab::lab {

data::FidelityClass fidelityFromName(std::string_view s) {
    using F = data::FidelityClass;
    if (s == "Exact")
        return F::Exact;
    if (s == "Numerical")
        return F::Numerical;
    if (s == "Statistical")
        return F::Statistical;
    if (s == "Illustrative")
        return F::Illustrative;
    return F::Model;
}

std::size_t utf8Length(std::string_view s) {
    std::size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80)
            ++n; // count lead bytes only
    return n;
}

double ComponentDescriptor::geometryNumber(std::string_view key, double fallback) const {
    auto it = geometry.find(key);
    return it != geometry.end() && it->is_number() ? it->get<double>() : fallback;
}
std::string ComponentDescriptor::geometryString(std::string_view key,
                                                std::string_view fallback) const {
    auto it = geometry.find(key);
    return it != geometry.end() && it->is_string() ? it->get<std::string>() : std::string(fallback);
}

Detail ComponentDescriptor::detailAt(double distance_m) const {
    for (const auto& r : lod)
        if (distance_m <= r.maxDistance_m)
            return r.detail;
    return lod.empty() ? Detail::Full : lod.back().detail;
}

const SpecRow* ComponentDescriptor::firstLiveRow() const {
    for (const auto& r : specSheet)
        if (r.binding)
            return &r;
    return nullptr;
}

namespace {

std::size_t wordCount(const std::string& s) {
    std::istringstream is(s);
    std::string w;
    std::size_t n = 0;
    while (is >> w)
        ++n;
    return n;
}

Result<std::string> requiredString(const core::Json& j, const char* key,
                                   const std::filesystem::path& from) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_string() || it->get<std::string>().empty())
        return fail(kErrDescriptor,
                    std::format("{}: missing required string '{}'", from.string(), key));
    return it->get<std::string>();
}

std::vector<std::string> stringArray(const core::Json& j, const char* key) {
    std::vector<std::string> out;
    if (auto it = j.find(key); it != j.end() && it->is_array())
        for (const auto& e : *it)
            if (e.is_string())
                out.push_back(e.get<std::string>());
    return out;
}

Result<SpecRow> parseRow(const core::Json& r, const std::filesystem::path& from) {
    SpecRow row;
    row.field = r.value("field", std::string{});
    if (row.field.empty())
        return fail(kErrDescriptor,
                    std::format("{}: spec_sheet row without 'field'", from.string()));
    if (auto u = r.find("unit"); u != r.end() && u->is_string()) {
        row.unit = u->get<std::string>();
        row.unitDeclared = true;
    }
    if (auto t = r.find("typical"); t != r.end() && t->is_array() && t->size() == 2 &&
                                    (*t)[0].is_number() && (*t)[1].is_number())
        row.typical = std::pair<double, double>{(*t)[0].get<double>(), (*t)[1].get<double>()};
    if (auto c = r.find("class"); c != r.end() && c->is_string()) {
        row.cls = fidelityFromName(c->get<std::string>());
        row.classDeclared = true;
    }
    if (auto b = r.find("binding"); b != r.end() && b->is_string()) {
        row.binding = b->get<std::string>();
        row.simulatorOnly = isSimulatorOnlyPath(*row.binding);
    }
    return row;
}

} // namespace

Result<ComponentDescriptor> ComponentCatalog::parse(const core::Json& j,
                                                    const std::filesystem::path& from) {
    ComponentDescriptor d;
    QXL_TRY_ASSIGN(d.id, requiredString(j, "id", from));
    QXL_TRY_ASSIGN(d.name, requiredString(j, "name", from));
    QXL_TRY_ASSIGN(d.category, requiredString(j, "category", from));
    QXL_TRY_ASSIGN(d.function, requiredString(j, "function", from));
    auto optionalString = [&j](const char* key) {
        auto it = j.find(key);
        return it != j.end() && it->is_string() ? it->get<std::string>() : std::string{};
    };
    d.parent = optionalString("parent");
    d.tooltip = optionalString("tooltip");
    d.modelName = optionalString("model_name");
    // `instrument` is an object {class, settings_schema, channels}; a bare string names the class.
    if (auto ins = j.find("instrument"); ins != j.end() && !ins->is_null()) {
        InstrumentInfo info;
        if (ins->is_string()) {
            info.kind = ins->get<std::string>();
        } else if (ins->is_object()) {
            info.kind = ins->value("class", std::string{});
            info.settingsSchema = ins->value("settings_schema", std::string{});
            info.channels = stringArray(*ins, "channels");
        }
        if (info.kind.empty())
            return fail(kErrDescriptor,
                        std::format("{}: 'instrument' has no 'class'", from.string()));
        d.instrument = std::move(info);
    }

    auto phys = j.find("physics");
    if (phys == j.end() || !phys->is_object())
        return fail(kErrDescriptor, std::format("{}: missing 'physics' object", from.string()));
    d.physicsSummary = phys->value("summary", std::string{});
    if (d.physicsSummary.empty())
        return fail(kErrDescriptor, std::format("{}: missing 'physics.summary'", from.string()));
    d.equationIds = stringArray(*phys, "equations");

    auto sheet = j.find("spec_sheet");
    if (sheet == j.end() || !sheet->is_array() || sheet->empty())
        return fail(kErrDescriptor,
                    std::format("{}: 'spec_sheet' must have at least one row", from.string()));
    for (const auto& r : *sheet) {
        QXL_TRY_ASSIGN(SpecRow row, parseRow(r, from));
        d.simulatorOnly = d.simulatorOnly || row.simulatorOnly;
        d.specSheet.push_back(std::move(row));
    }
    d.theoryAnchors = stringArray(j, "theory");

    auto geo = j.find("geometry");
    if (geo == j.end() || !geo->is_object())
        return fail(kErrDescriptor, std::format("{}: missing 'geometry' object", from.string()));
    d.geometry = *geo;
    d.generator = geo->value("generator", std::string{});
    if (d.generator.empty())
        return fail(kErrDescriptor, std::format("{}: missing 'geometry.generator'", from.string()));

    auto lod = j.find("lod");
    if (lod == j.end() || !lod->is_array() || lod->empty())
        return fail(kErrDescriptor,
                    std::format("{}: 'lod' must have at least one level", from.string()));
    for (const auto& l : *lod) {
        LodRule r;
        r.maxDistance_m = l.value("max_m", 1e9);
        std::string det = l.value("detail", std::string("full"));
        if (!detailFromName(det, r.detail))
            return fail(kErrDescriptor,
                        std::format("{}: unknown lod detail '{}'", from.string(), det));
        d.lod.push_back(r);
    }
    std::stable_sort(d.lod.begin(), d.lod.end(), [](const LodRule& a, const LodRule& b) {
        return a.maxDistance_m < b.maxDistance_m;
    });
    return d;
}

Status ComponentCatalog::validate(const ComponentDescriptor& d) {
    std::vector<std::string> problems;
    std::size_t words = wordCount(d.function);
    if (words < 40 || words > 200)
        problems.push_back(
            std::format("'function' has {} words, spec 17 §4 requires 40..200", words));
    std::size_t chars = utf8Length(d.tooltip);
    if (chars == 0 || chars > 90)
        problems.push_back(
            std::format("'tooltip' has {} characters, spec 17 §4 requires 1..90", chars));
    if (d.theoryAnchors.empty() && d.category != "structure" && d.category != "infra")
        problems.push_back("'theory' needs at least one anchor for a non-structural component");
    for (const auto& r : d.specSheet) {
        if (!r.unitDeclared)
            problems.push_back(std::format("row '{}' has no 'unit'", r.field));
        if (!r.binding)
            continue;
        if (!r.classDeclared)
            problems.push_back(std::format("live row '{}' has no 'class'", r.field));
        if (auto root = splitBindingRoot(*r.binding); !root)
            problems.push_back(std::format("row '{}': {}", r.field, root.error().message));
    }
    if (d.lod.empty())
        problems.push_back("'lod' is empty");
    if (problems.empty())
        return {};
    Error e(kErrDescriptor, std::format("component '{}' fails lint", d.id));
    for (auto& p : problems)
        e.withNote(std::move(p));
    return std::unexpected(std::move(e));
}

Result<ComponentCatalog> ComponentCatalog::load(const std::filesystem::path& dirIn) {
    std::filesystem::path dir = dirIn.empty() ? core::assetDir() / "Lab" / "Components" : dirIn;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec))
        return fail(kErrDescriptor, "component directory not found: " + dir.string());

    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        auto f = e.path() / "component.json";
        if (std::filesystem::exists(f, ec))
            files.push_back(f);
    }
    std::sort(files.begin(), files.end()); // deterministic catalog order

    ComponentCatalog cat;
    cat.items_.reserve(files.size());
    for (const auto& f : files) {
        QXL_TRY_ASSIGN(auto env, core::JsonEnvelope::load(f, "lab.component"));
        QXL_TRY_ASSIGN(auto d, parse(env.data, f));
        if (auto st = validate(d); !st) {
            st.error().notes.push_back("file: " + f.string());
            return std::unexpected(std::move(st.error()));
        }
        if (d.id != f.parent_path().filename().string())
            return fail(kErrDescriptor,
                        std::format("{}: id '{}' does not match its directory", f.string(), d.id));
        if (cat.byId_.contains(d.id))
            return fail(kErrDescriptor, std::format("duplicate component id '{}'", d.id));
        cat.byId_.emplace(d.id, cat.items_.size());
        cat.items_.push_back(std::move(d));
    }
    if (cat.items_.empty())
        return fail(kErrDescriptor, "no component.json found under " + dir.string());
    return cat;
}

const ComponentDescriptor* ComponentCatalog::find(std::string_view id) const {
    auto it = byId_.find(id);
    return it == byId_.end() ? nullptr : &items_[it->second];
}

std::vector<std::string> ComponentCatalog::ids() const {
    std::vector<std::string> v;
    v.reserve(items_.size());
    for (const auto& d : items_)
        v.push_back(d.id);
    return v;
}

} // namespace qlab::lab
