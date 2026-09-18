#include "Core/Json.hpp"
#include "Core/Version.hpp"
#include <chrono>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
namespace qlab::core {
namespace {
struct KindInfo { int schema; std::vector<Upgrader> up; };
std::map<std::string, KindInfo, std::less<>>& registry() { static std::map<std::string, KindInfo, std::less<>> r; return r; }
std::mutex& regMutex() { static std::mutex m; return m; }
} // namespace

void JsonEnvelope::registerKind(std::string kind, int cur, std::vector<Upgrader> up) {
    std::lock_guard lk(regMutex());
    registry()[std::move(kind)] = {cur, std::move(up)};
}
int JsonEnvelope::currentSchema(std::string_view kind) {
    std::lock_guard lk(regMutex());
    auto it = registry().find(kind);
    return it == registry().end() ? 1 : it->second.schema;
}
Result<Envelope> JsonEnvelope::parse(const std::string& text, std::string_view expectedKind) {
    Json j = Json::parse(text, nullptr, false);
    if (j.is_discarded()) return fail(ErrorCode::Parse, "malformed JSON");
    if (!j.is_object() || !j.contains("qxl") || !j["qxl"].is_object() || !j.contains("data"))
        return fail(ErrorCode::Parse, "missing qxl envelope");
    Envelope e;
    const Json& h = j["qxl"];
    e.kind = h.value("kind", "");
    e.schema = h.value("schema", 1);
    e.app = h.value("app", "");
    e.created = h.value("created", "");
    e.data = j["data"];
    if (!expectedKind.empty() && e.kind != expectedKind)
        return fail(ErrorCode::InvalidArgument, std::format("expected kind '{}', found '{}'", expectedKind, e.kind));
    KindInfo info{1, {}};
    { std::lock_guard lk(regMutex()); auto it = registry().find(e.kind); if (it != registry().end()) info = it->second; }
    if (e.schema > info.schema)
        return fail(ErrorCode::Unsupported, std::format("'{}' schema {} is newer than supported {} (written by app {})", e.kind, e.schema, info.schema, e.app));
    while (e.schema < info.schema) {
        std::size_t idx = static_cast<std::size_t>(e.schema - 1);
        if (idx >= info.up.size()) return fail(ErrorCode::Unsupported, std::format("no upgrader from schema {} for '{}'", e.schema, e.kind));
        auto r = info.up[idx](std::move(e.data));
        if (!r) return std::unexpected(r.error());
        e.data = std::move(*r);
        ++e.schema;
    }
    return e;
}
Result<Envelope> JsonEnvelope::load(const std::filesystem::path& path, std::string_view kind) {
    auto t = readTextFile(path);
    if (!t) return std::unexpected(t.error());
    auto r = parse(*t, kind);
    if (!r) r.error().notes.push_back("file: " + path.string());
    return r;
}
std::string JsonEnvelope::serialize(std::string_view kind, const Json& data, int schema) {
    Json j;
    j["qxl"] = {{"kind", std::string(kind)}, {"schema", schema < 0 ? currentSchema(kind) : schema},
                {"app", std::string(version())}, {"created", isoNow()}};
    j["data"] = data;
    return j.dump(2);
}
Status JsonEnvelope::save(const std::filesystem::path& path, std::string_view kind, const Json& data) {
    return writeTextFileAtomic(path, serialize(kind, data));
}
Result<std::string> readTextFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return fail(ErrorCode::Io, "cannot open " + p.string());
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}
Status writeTextFileAtomic(const std::filesystem::path& p, std::string_view text) {
    std::error_code ec;
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    auto tmp = p; tmp += ".tmp";
    { std::ofstream f(tmp, std::ios::binary | std::ios::trunc); if (!f) return fail(ErrorCode::Io, "cannot write " + tmp.string()); f << text; }
    std::filesystem::rename(tmp, p, ec);
    if (ec) return fail(ErrorCode::Io, "rename failed: " + ec.message());
    return {};
}
std::string isoNow() {
    auto now = std::chrono::system_clock::now();
    return std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(now));
}
Result<double> jsonQuantity(const Json& j, std::string_view expectedUnit) {
    if (j.is_number()) return j.get<double>();
    if (j.is_object() && j.contains("value")) {
        if (!expectedUnit.empty() && j.contains("unit") && j["unit"].get<std::string>() != expectedUnit)
            return fail(ErrorCode::InvalidArgument, std::format("unit '{}' where '{}' expected", j["unit"].get<std::string>(), expectedUnit));
        return j["value"].get<double>();
    }
    return fail(ErrorCode::Parse, "quantity must be a number or {value, unit}");
}
} // namespace qlab::core
