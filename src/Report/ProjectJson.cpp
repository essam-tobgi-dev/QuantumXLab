// Spec 23 §2 — the `data` object of a `.qxlab` file. Every record is parsed by taking a copy of its
// JSON object, removing the fields this version knows and keeping the rest in `extra`, which is
// written back first on save: a project written by a newer application survives save → load → save
// byte for byte (§1 forward compatibility, §12 acceptance).
#include "Report/Project.hpp"

#include <algorithm>
#include <charconv>
#include <format>

namespace qlab::report {
namespace {

using core::Json;

Json objectOr(const Json& j, const char* key) {
    if (!j.is_object() || !j.contains(key)) return Json::object();
    const Json& v = j[key];
    return v.is_object() ? v : Json::object();
}

// Takes `key` out of `rest` and returns it (an absent or wrong-typed field yields `fallback`).
std::string takeString(Json& rest, const char* key, std::string fallback = {}) {
    auto it = rest.find(key);
    if (it == rest.end()) return fallback;
    std::string v = it->is_string() ? it->get<std::string>() : fallback;
    rest.erase(it);
    return v;
}
bool takeBool(Json& rest, const char* key, bool fallback = false) {
    auto it = rest.find(key);
    if (it == rest.end()) return fallback;
    bool v = it->is_boolean() ? it->get<bool>() : fallback;
    rest.erase(it);
    return v;
}
// Only a non-negative integer is accepted: a negative value would wrap into a huge count.
std::uint64_t takeUint(Json& rest, const char* key, std::uint64_t fallback = 0) {
    auto it = rest.find(key);
    if (it == rest.end()) return fallback;
    std::uint64_t v = it->is_number_unsigned() ? it->get<std::uint64_t>() : fallback;
    rest.erase(it);
    return v;
}
int takeInt(Json& rest, const char* key, int fallback) {
    auto it = rest.find(key);
    if (it == rest.end()) return fallback;
    int v = it->is_number_integer() || it->is_number_unsigned() ? it->get<int>() : fallback;
    rest.erase(it);
    return v;
}
Json takeJson(Json& rest, const char* key, Json fallback) {
    auto it = rest.find(key);
    if (it == rest.end()) return fallback;
    Json v = *it;
    rest.erase(it);
    return v;
}
constexpr std::uint64_t kMaxRunNumber = 999999999ull;   // ids stay readable and `next` never wraps

// An `extra` object is written back field by field so a known key can never be shadowed.
Json withExtra(const Json& extra, Json fields) {
    Json out = extra.is_object() ? extra : Json::object();
    for (auto& [k, v] : fields.items()) out[k] = v;
    return out;
}

} // namespace

// ---------------------------------------------------------------- records

static Json programJson(const ProgramRef& p) {
    Json j;
    if (!p.path.empty()) j["path"] = p.path;
    if (!p.inlineSource.empty()) j["inline"] = p.inlineSource;
    j["isMain"] = p.isMain;
    return withExtra(p.extra, std::move(j));
}
static ProgramRef programFrom(Json rest) {
    ProgramRef p;
    p.path = takeString(rest, "path");
    p.inlineSource = takeString(rest, "inline");
    p.isMain = takeBool(rest, "isMain");
    p.extra = std::move(rest);
    return p;
}

static Json deviceJson(const DeviceRef& d) {
    Json j;
    j["id"] = d.id;
    j["calibrationOverrides"] = d.calibrationOverrides;
    j["wiringOverrides"] = d.wiringOverrides;
    return withExtra(d.extra, std::move(j));
}
static DeviceRef deviceFrom(Json rest) {
    DeviceRef d;
    d.id = takeString(rest, "id");
    d.calibrationOverrides = takeJson(rest, "calibrationOverrides", Json::object());
    d.wiringOverrides = takeJson(rest, "wiringOverrides", Json::object());
    d.extra = std::move(rest);
    return d;
}

static Json backendJson(const BackendSettings& b) {
    Json j;
    j["kind"] = b.kind;
    j["shots"] = b.shots;
    j["seed"] = b.seed;
    j["noise"] = b.noise;
    j["snapshotEveryGate"] = b.snapshotEveryGate;
    j["lindblad"] = b.lindblad;
    return withExtra(b.extra, std::move(j));
}
static BackendSettings backendFrom(Json rest) {
    BackendSettings b;
    b.kind = takeString(rest, "kind", b.kind);
    b.shots = static_cast<std::uint32_t>(takeUint(rest, "shots", b.shots));
    b.seed = takeUint(rest, "seed", b.seed);
    b.noise = takeBool(rest, "noise", b.noise);
    b.snapshotEveryGate = takeBool(rest, "snapshotEveryGate", b.snapshotEveryGate);
    b.lindblad = takeJson(rest, "lindblad", Json::object());
    b.extra = std::move(rest);
    return b;
}

static Json compilerJson(const CompilerSettings& c) {
    Json j;
    j["optimizationLevel"] = c.optimizationLevel;
    j["layout"] = c.layout;
    j["routing"] = c.routing;
    j["scheduling"] = c.scheduling;
    j["passOverrides"] = c.passOverrides;
    return withExtra(c.extra, std::move(j));
}
static CompilerSettings compilerFrom(Json rest) {
    CompilerSettings c;
    c.optimizationLevel = takeInt(rest, "optimizationLevel", c.optimizationLevel);
    c.layout = takeString(rest, "layout", c.layout);
    c.routing = takeString(rest, "routing", c.routing);
    c.scheduling = takeString(rest, "scheduling", c.scheduling);
    c.passOverrides = takeJson(rest, "passOverrides", Json::object());
    c.extra = std::move(rest);
    return c;
}

static Json workspaceJson(const WorkspaceState& w) {
    Json j;
    j["preset"] = w.preset;
    j["imguiLayout"] = w.imguiLayout;
    j["camera"] = w.camera;
    j["openPanels"] = w.openPanels;
    return withExtra(w.extra, std::move(j));
}
static WorkspaceState workspaceFrom(Json rest) {
    WorkspaceState w;
    w.preset = takeString(rest, "preset");
    w.imguiLayout = takeString(rest, "imguiLayout");
    w.camera = takeJson(rest, "camera", Json::object());
    w.openPanels = takeJson(rest, "openPanels", Json::array());
    w.extra = std::move(rest);
    return w;
}

static Json runJson(const RunSummary& r) {
    Json j;
    j["id"] = r.id;
    j["time"] = r.time;
    j["backend"] = r.backend;
    j["device"] = r.device;
    j["programHash"] = r.programHash;
    j["shots"] = r.shots;
    j["seed"] = r.seed;
    j["resultPath"] = r.resultPath;
    j["estimate"] = r.estimate;
    return withExtra(r.extra, std::move(j));
}
static RunSummary runFrom(Json rest) {
    RunSummary r;
    r.id = takeString(rest, "id");
    r.time = takeString(rest, "time");
    r.backend = takeString(rest, "backend");
    r.device = takeString(rest, "device");
    r.programHash = takeString(rest, "programHash");
    r.shots = static_cast<std::uint32_t>(takeUint(rest, "shots"));
    r.seed = takeUint(rest, "seed");
    r.resultPath = takeString(rest, "resultPath");
    r.estimate = takeJson(rest, "estimate", Json::object());
    r.extra = std::move(rest);
    return r;
}

static Json recipeJson(const RecipeRef& r) {
    Json j;
    j["id"] = r.id;
    if (r.qubit) j["qubit"] = *r.qubit;
    j["lastResultPath"] = r.lastResultPath;
    return withExtra(r.extra, std::move(j));
}
static RecipeRef recipeFrom(Json rest) {
    RecipeRef r;
    r.id = takeString(rest, "id");
    if (auto it = rest.find("qubit"); it != rest.end()) {
        if (it->is_number_unsigned() || it->is_number_integer()) r.qubit = it->get<std::uint32_t>();
        rest.erase(it);
    }
    r.lastResultPath = takeString(rest, "lastResultPath");
    r.extra = std::move(rest);
    return r;
}

// ---------------------------------------------------------------- project

core::Json Project::toJson() const {
    Json j;
    j["name"] = name;
    j["programs"] = Json::array();
    for (const auto& p : programs) j["programs"].push_back(programJson(p));
    j["device"] = deviceJson(device);
    j["backend"] = backendJson(backend);
    j["compiler"] = compilerJson(compiler);
    j["workspace"] = workspaceJson(workspace);
    j["runs"] = Json::array();
    for (const auto& r : runs) j["runs"].push_back(runJson(r));
    j["recipes"] = Json::array();
    for (const auto& r : recipes) j["recipes"].push_back(recipeJson(r));
    return withExtra(extra, std::move(j));
}

Result<Project> Project::fromJson(const core::Json& data) {
    if (!data.is_object()) return fail(ErrorCode::Parse, "project: 'data' must be an object");
    Json rest = data;
    Project p;
    p.name = takeString(rest, "name");
    if (auto it = rest.find("programs"); it != rest.end()) {
        if (!it->is_array()) return fail(ErrorCode::Parse, "project: 'data.programs' must be an array");
        for (const auto& e : *it)
            if (e.is_object()) p.programs.push_back(programFrom(e));
        rest.erase(it);
    }
    p.device = deviceFrom(objectOr(rest, "device"));
    rest.erase("device");
    p.backend = backendFrom(objectOr(rest, "backend"));
    rest.erase("backend");
    p.compiler = compilerFrom(objectOr(rest, "compiler"));
    rest.erase("compiler");
    p.workspace = workspaceFrom(objectOr(rest, "workspace"));
    rest.erase("workspace");
    if (auto it = rest.find("runs"); it != rest.end()) {
        if (!it->is_array()) return fail(ErrorCode::Parse, "project: 'data.runs' must be an array");
        for (const auto& e : *it)
            if (e.is_object()) p.runs.push_back(runFrom(e));
        rest.erase(it);
    }
    if (auto it = rest.find("recipes"); it != rest.end()) {
        if (!it->is_array()) return fail(ErrorCode::Parse, "project: 'data.recipes' must be an array");
        for (const auto& e : *it)
            if (e.is_object()) p.recipes.push_back(recipeFrom(e));
        rest.erase(it);
    }
    p.extra = std::move(rest);
    return p;
}

const ProgramRef* Project::mainProgram() const {
    for (const auto& p : programs)
        if (p.isMain) return &p;
    return programs.empty() ? nullptr : &programs.front();
}

const RunSummary* Project::run(std::string_view id) const {
    for (const auto& r : runs)
        if (r.id == id) return &r;
    return nullptr;
}

std::string Project::addRun(RunSummary summary) {
    if (summary.id.empty()) {
        std::uint64_t next = 1;
        for (const auto& r : runs) {
            if (!r.id.starts_with("run-")) continue;
            // `from_chars` never throws: an id too long to be a number simply does not raise `next`.
            std::uint64_t n = 0;
            const char* first = r.id.data() + 4;
            const char* last = r.id.data() + r.id.size();
            const auto parsed = std::from_chars(first, last, n);
            if (parsed.ec == std::errc{} && parsed.ptr == last && n < kMaxRunNumber) next = std::max(next, n + 1);
        }
        summary.id = std::format("run-{:06}", next);
    }
    if (summary.resultPath.empty()) summary.resultPath = "runs/" + summary.id + ".json";
    runs.push_back(std::move(summary));
    return runs.back().id;
}

} // namespace qlab::report
