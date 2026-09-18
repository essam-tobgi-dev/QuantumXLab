#pragma once
// Spec 04 §8 — JSON envelope { "qxl": {kind, schema, app, created}, "data": {...} } with upgrade chains.
#include "Core/Error.hpp"
#include <filesystem>
#include <functional>
#include <nlohmann/json.hpp>
namespace qlab::core {
using Json = nlohmann::json;
struct Envelope { std::string kind; int schema = 1; std::string app; std::string created; Json data; };
using Upgrader = std::function<Result<Json>(Json)>; // v -> v+1
class JsonEnvelope {
public:
    static void registerKind(std::string kind, int currentSchema, std::vector<Upgrader> upgraders = {});
    static Result<Envelope> parse(const std::string& text, std::string_view expectedKind);
    static Result<Envelope> load(const std::filesystem::path& path, std::string_view expectedKind);
    static std::string serialize(std::string_view kind, const Json& data, int schema = -1);
    static Status save(const std::filesystem::path& path, std::string_view kind, const Json& data);
    static int currentSchema(std::string_view kind);
};
Result<std::string> readTextFile(const std::filesystem::path& p);
Status writeTextFileAtomic(const std::filesystem::path& p, std::string_view text);
std::string isoNow();
// Quantity encoding used in device/calibration files (spec 05 §7): {"value": x, "unit": "GHz"}.
Result<double> jsonQuantity(const Json& j, std::string_view expectedUnit);
} // namespace qlab::core
