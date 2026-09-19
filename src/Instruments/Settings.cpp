#include "Instruments/Settings.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace qlab::instr {

std::string_view settingTypeName(SettingType t) {
    switch (t) {
    case SettingType::Bool:
        return "boolean";
    case SettingType::Int:
        return "integer";
    case SettingType::Real:
        return "number";
    case SettingType::Enum:
        return "string";
    case SettingType::Text:
        return "string";
    }
    return "?";
}

SettingSpec SettingSpec::real(std::string key, std::string unit, double min, double max,
                              double step, double def, std::string description) {
    SettingSpec s;
    s.key = std::move(key);
    s.type = SettingType::Real;
    s.unit = std::move(unit);
    s.min = min;
    s.max = max;
    s.step = step;
    s.defaultValue = def;
    s.description = std::move(description);
    return s;
}
SettingSpec SettingSpec::integer(std::string key, std::string unit, std::int64_t min,
                                 std::int64_t max, std::int64_t def, std::string description) {
    SettingSpec s;
    s.key = std::move(key);
    s.type = SettingType::Int;
    s.unit = std::move(unit);
    s.min = static_cast<double>(min);
    s.max = static_cast<double>(max);
    s.step = 1.0;
    s.defaultValue = def;
    s.description = std::move(description);
    return s;
}
SettingSpec SettingSpec::boolean(std::string key, bool def, std::string description) {
    SettingSpec s;
    s.key = std::move(key);
    s.type = SettingType::Bool;
    s.defaultValue = def;
    s.description = std::move(description);
    return s;
}
SettingSpec SettingSpec::choice(std::string key, std::vector<std::string> options, std::string def,
                                std::string description) {
    SettingSpec s;
    s.key = std::move(key);
    s.type = SettingType::Enum;
    s.options = std::move(options);
    s.defaultValue = std::move(def);
    s.description = std::move(description);
    return s;
}
SettingSpec SettingSpec::text(std::string key, std::string def, std::string description) {
    SettingSpec s;
    s.key = std::move(key);
    s.type = SettingType::Text;
    s.defaultValue = std::move(def);
    s.description = std::move(description);
    return s;
}
SettingSpec SettingSpec::discrete(std::string key, std::string unit, std::vector<double> allowed,
                                  double def, std::string description) {
    SettingSpec s;
    s.key = std::move(key);
    s.type = SettingType::Real;
    s.unit = std::move(unit);
    s.allowed = std::move(allowed);
    if (!s.allowed.empty()) {
        s.min = *std::min_element(s.allowed.begin(), s.allowed.end());
        s.max = *std::max_element(s.allowed.begin(), s.allowed.end());
    }
    s.defaultValue = def;
    s.description = std::move(description);
    return s;
}

SettingSpec refreshRateSetting() {
    return SettingSpec::real("refresh_hz", "Hz", 0.1, 60.0, 0.0, 10.0,
                             "Live-mode acquisition rate");
}

const SettingSpec* SettingSchema::find(std::string_view key) const {
    for (auto const& s : settings)
        if (s.key == key)
            return &s;
    return nullptr;
}

namespace {
// Range clamp, discrete snap and step rounding of a numeric request.
double coerceNumber(const SettingSpec& spec, double x, Coerced& out) {
    const std::string unit = spec.unit.empty() ? "" : " " + spec.unit;
    if (x < spec.min) {
        out.clamped = true;
        out.message = std::format("{} {:.6g}{} below the minimum {:.6g}{}: clamped", spec.key, x,
                                  unit, spec.min, unit);
        x = spec.min;
    } else if (x > spec.max) {
        out.clamped = true;
        out.message = std::format("{} {:.6g}{} above the maximum {:.6g}{}: clamped", spec.key, x,
                                  unit, spec.max, unit);
        x = spec.max;
    }
    if (!spec.allowed.empty()) {
        double best = spec.allowed.front();
        for (double a : spec.allowed)
            if (std::abs(a - x) < std::abs(best - x))
                best = a;
        if (std::abs(best - x) > 1e-12 * std::max(1.0, std::abs(best)) && !out.clamped) {
            out.clamped = true;
            out.message = std::format("{} {:.6g}{} is not an available value: set to {:.6g}{}",
                                      spec.key, x, unit, best, unit);
        }
        return best;
    }
    if (spec.step > 0.0) { // instrument resolution (silent). Decimal steps round through 1/step so
        const double inv =
            std::round(1.0 / spec.step); // that 5 dBm on a 0.01 dB grid stays exactly 5.0
        if (spec.step < 1.0 && std::abs(inv * spec.step - 1.0) < 1e-9)
            x = std::round(x * inv) / inv;
        else
            x = std::round(x / spec.step) * spec.step;
        x = std::clamp(x, spec.min, spec.max);
    }
    return x;
}
} // namespace

Result<Coerced> SettingSchema::coerce(std::string_view key, const SettingValue& v,
                                      bool allowReadOnly) const {
    const SettingSpec* spec = find(key);
    if (!spec)
        return fail(err::UnknownSetting, std::format("{}: no setting named '{}'", instrument, key));
    if (spec->readOnly && !allowReadOnly)
        return fail(
            err::BadSettingType,
            std::format("{}: '{}' is fixed by the hardware and cannot be set", instrument, key));
    Coerced out;
    auto wrong = [&](std::string_view want) {
        return fail(err::BadSettingType, std::format("{}: setting '{}' expects {}, got '{}'",
                                                     instrument, key, want, settingToString(v)));
    };
    switch (spec->type) {
    case SettingType::Bool: {
        if (auto* b = std::get_if<bool>(&v)) {
            out.value = *b;
            return out;
        }
        if (auto* i = std::get_if<std::int64_t>(&v); i && (*i == 0 || *i == 1)) {
            out.value = *i == 1;
            return out;
        }
        return wrong("a boolean");
    }
    case SettingType::Int:
    case SettingType::Real: {
        auto num = std::holds_alternative<bool>(v) ? std::nullopt : settingNumber(v);
        if (!num)
            return wrong("a number");
        if (!std::isfinite(*num))
            return wrong("a finite number");
        double x = coerceNumber(*spec, *num, out);
        if (spec->type == SettingType::Int)
            out.value = static_cast<std::int64_t>(std::llround(x));
        else
            out.value = x;
        return out;
    }
    case SettingType::Enum: {
        auto* s = std::get_if<std::string>(&v);
        if (!s)
            return wrong("one of its options");
        if (std::find(spec->options.begin(), spec->options.end(), *s) == spec->options.end()) {
            std::string list;
            for (auto const& o : spec->options)
                list += (list.empty() ? "" : " | ") + o;
            return fail(err::BadSettingType, std::format("{}: '{}' is not an option of '{}' ({})",
                                                         instrument, *s, key, list));
        }
        out.value = *s;
        return out;
    }
    case SettingType::Text: {
        auto* s = std::get_if<std::string>(&v);
        if (!s)
            return wrong("text");
        out.value = *s;
        return out;
    }
    }
    return wrong("a valid value");
}

core::Json settingValueToJson(const SettingValue& v) {
    if (auto* b = std::get_if<bool>(&v))
        return *b;
    if (auto* i = std::get_if<std::int64_t>(&v))
        return *i;
    if (auto* d = std::get_if<double>(&v))
        return *d;
    if (auto* s = std::get_if<std::string>(&v))
        return *s;
    return nullptr;
}

SettingValue settingValueFromJson(const core::Json& j) {
    if (j.is_boolean())
        return j.get<bool>();
    if (j.is_number_integer())
        return j.get<std::int64_t>();
    if (j.is_number())
        return j.get<double>();
    if (j.is_string())
        return j.get<std::string>();
    return std::monostate{};
}

core::Json SettingSchema::toJson() const {
    core::Json props = core::Json::object();
    core::Json order = core::Json::array();
    for (auto const& s : settings) {
        core::Json p = core::Json::object();
        p["type"] = std::string(settingTypeName(s.type));
        if (!s.unit.empty())
            p["unit"] = s.unit;
        const bool numeric = s.type == SettingType::Int || s.type == SettingType::Real;
        if (numeric && std::isfinite(s.min))
            p["minimum"] = s.min;
        if (numeric && std::isfinite(s.max))
            p["maximum"] = s.max;
        if (numeric && s.step > 0.0)
            p["multipleOf"] = s.step;
        if (!s.allowed.empty())
            p["x-allowed"] = s.allowed;
        if (s.type == SettingType::Enum)
            p["enum"] = s.options;
        p["default"] = settingValueToJson(s.defaultValue);
        if (!s.description.empty())
            p["description"] = s.description;
        if (s.readOnly)
            p["readOnly"] = true;
        props[s.key] = std::move(p);
        order.push_back(s.key);
    }
    return core::Json{{"$id", id},
                      {"title", instrument},
                      {"type", "object"},
                      {"x-order", order},
                      {"properties", props}};
}

Result<SettingSchema> SettingSchema::fromJson(const core::Json& j) {
    if (!j.is_object())
        return fail(err::BadSchema, "settings schema: expected an object");
    if (!j.contains("properties") || !j["properties"].is_object())
        return fail(err::BadSchema, "settings schema: 'properties' missing");
    SettingSchema out;
    out.id = j.value("$id", std::string{});
    out.instrument = j.value("title", std::string{});
    const core::Json& props = j["properties"];
    std::vector<std::string> keys;
    if (j.contains("x-order") && j["x-order"].is_array())
        for (auto const& k : j["x-order"])
            if (k.is_string() && props.contains(k.get<std::string>()))
                keys.push_back(k.get<std::string>());
    for (auto const& [k, v] :
         props.items()) { // properties the order list does not mention keep file order
        (void)v;
        if (std::find(keys.begin(), keys.end(), k) == keys.end())
            keys.push_back(k);
    }
    for (auto const& key : keys) {
        const core::Json& p = props[key];
        const std::string path = "properties." + key;
        if (!p.is_object() || !p.contains("type") || !p["type"].is_string())
            return fail(err::BadSchema, std::format("settings schema: '{}.type' missing", path));
        SettingSpec s;
        s.key = key;
        const std::string type = p["type"].get<std::string>();
        if (type == "boolean")
            s.type = SettingType::Bool;
        else if (type == "integer")
            s.type = SettingType::Int;
        else if (type == "number")
            s.type = SettingType::Real;
        else if (type == "string")
            s.type = p.contains("enum") ? SettingType::Enum : SettingType::Text;
        else
            return fail(
                err::BadSchema,
                std::format("settings schema: '{}.type' = '{}' is not supported", path, type));
        s.unit = p.value("unit", std::string{});
        if (p.contains("minimum"))
            s.min = p["minimum"].get<double>();
        if (p.contains("maximum"))
            s.max = p["maximum"].get<double>();
        if (p.contains("multipleOf"))
            s.step = p["multipleOf"].get<double>();
        if (p.contains("x-allowed"))
            s.allowed = p["x-allowed"].get<std::vector<double>>();
        if (p.contains("enum"))
            s.options = p["enum"].get<std::vector<std::string>>();
        if (!p.contains("default"))
            return fail(err::BadSchema, std::format("settings schema: '{}.default' missing", path));
        s.defaultValue = settingValueFromJson(p["default"]);
        if (s.type == SettingType::Real) // a whole-number default is still a real setting
            if (auto n = settingNumber(s.defaultValue))
                s.defaultValue = *n;
        s.description = p.value("description", std::string{});
        s.readOnly = p.value("readOnly", false);
        out.settings.push_back(std::move(s));
    }
    return out;
}

} // namespace qlab::instr
