#include "Pulse/Expr.hpp"
#include "Pulse/Errors.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <numbers>

namespace qlab::pulse {
namespace {

struct Parser {
    std::string_view s;
    std::size_t i = 0;
    const ParamMap& params;
    const PathResolver& paths;
    std::optional<Error> err;

    void skip() {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
            ++i;
    }
    bool eat(char c) {
        skip();
        if (i < s.size() && s[i] == c) {
            ++i;
            return true;
        }
        return false;
    }
    void setErr(std::string m) {
        if (!err)
            err = Error(kErrLibrary, std::format("{} in expression '{}'", m, s));
    }

    double parseExpr() {
        double v = parseTerm();
        for (;;) {
            skip();
            if (eat('+'))
                v += parseTerm();
            else if (i < s.size() && s[i] == '-') {
                ++i;
                v -= parseTerm();
            } else
                break;
        }
        return v;
    }
    double parseTerm() {
        double v = parseUnary();
        for (;;) {
            skip();
            if (eat('*'))
                v *= parseUnary();
            else if (eat('/')) {
                double d = parseUnary();
                if (d == 0.0) {
                    setErr("division by zero");
                    return 0.0;
                }
                v /= d;
            } else
                break;
        }
        return v;
    }
    double parseUnary() {
        skip();
        if (eat('-'))
            return -parseUnary();
        if (eat('+'))
            return parseUnary();
        return parseAtom();
    }
    double parseAtom() {
        skip();
        if (i >= s.size()) {
            setErr("unexpected end");
            return 0.0;
        }
        if (eat('(')) {
            double v = parseExpr();
            if (!eat(')'))
                setErr("missing ')'");
            return v;
        }
        const char c = s[i];
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            double v = 0.0;
            auto* first = s.data() + i;
            auto* last = s.data() + s.size();
            auto [ptr, ec] = std::from_chars(first, last, v);
            if (ec != std::errc{}) {
                setErr("bad number");
                return 0.0;
            }
            i = static_cast<std::size_t>(ptr - s.data());
            return v;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            const std::size_t start = i;
            std::size_t segment = i; // start of the current dotted segment
            for (;;) {
                while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) ||
                                        s[i] == '_' || s[i] == '.')) {
                    if (s[i] == '.')
                        segment = i + 1;
                    ++i;
                }
                // '-' continues the identifier only inside an edge key of a dotted path
                // ("cal.edges.0-1"): an all-digit segment followed by '-' and a digit.
                const std::string_view head = s.substr(start, i - start);
                const bool dotted = head.starts_with("cal.") || head.starts_with("device.");
                const std::string_view seg = s.substr(segment, i - segment);
                const bool digits =
                    !seg.empty() && std::all_of(seg.begin(), seg.end(), [](char ch) {
                        return std::isdigit(static_cast<unsigned char>(ch)) != 0;
                    });
                if (dotted && digits && i + 1 < s.size() && s[i] == '-' &&
                    std::isdigit(static_cast<unsigned char>(s[i + 1]))) {
                    ++i;
                    continue;
                }
                break;
            }
            std::string_view id = s.substr(start, i - start);
            if (id == "pi")
                return std::numbers::pi;
            if (id == "e")
                return std::numbers::e;
            if (auto it = params.find(id); it != params.end())
                return it->second;
            if (paths) {
                auto r = paths(id);
                if (r)
                    return *r;
                if (!err)
                    err = r.error();
                return 0.0;
            }
            setErr(std::format("unknown symbol '{}'", id));
            return 0.0;
        }
        setErr(std::format("unexpected character '{}'", c));
        return 0.0;
    }
};

} // namespace

Result<double> evalExpression(std::string_view text, const ParamMap& params,
                              const PathResolver& paths) {
    Parser p{text, 0, params, paths, std::nullopt};
    const double v = p.parseExpr();
    p.skip();
    if (p.err)
        return fail(*p.err);
    if (p.i != text.size())
        return fail(kErrLibrary, std::format("trailing text in expression '{}'", text));
    return v;
}

Result<double> evalJsonValue(const core::Json& j, const ParamMap& params,
                             const PathResolver& paths) {
    if (j.is_number())
        return j.get<double>();
    if (j.is_string())
        return evalExpression(j.get<std::string>(), params, paths);
    return fail(kErrLibrary, "value must be a number or an expression string");
}

PathResolver makeJsonResolver(const core::Json& calibration, const core::Json& device) {
    return [&calibration, &device](std::string_view path) -> Result<double> {
        std::string_view rest = path;
        const core::Json* node = nullptr;
        if (rest.starts_with("cal.")) {
            node = &calibration;
            rest.remove_prefix(4);
        } else if (rest.starts_with("device.")) {
            node = &device;
            rest.remove_prefix(7);
        } else
            return fail(kErrLibrary, std::format("unknown symbol '{}'", path));

        while (!rest.empty()) {
            const auto dot = rest.find('.');
            std::string key(rest.substr(0, dot));
            // An edge key such as "0-1" contains no dot; a qubit index is a bare number.
            if (node->is_array()) {
                std::size_t idx = 0;
                auto [ptr, ec] = std::from_chars(key.data(), key.data() + key.size(), idx);
                if (ec != std::errc{} || idx >= node->size())
                    return fail(kErrLibrary,
                                std::format("'{}' is not an index of '{}'", key, path));
                node = &(*node)[idx];
            } else if (node->is_object() && node->contains(key)) {
                node = &(*node)[key];
            } else {
                return fail(kErrLibrary, std::format("path '{}' has no member '{}'", path, key));
            }
            if (dot == std::string_view::npos)
                break;
            rest.remove_prefix(dot + 1);
        }
        // Calibration entries are [value, sigma, source] triples.
        if (node->is_array() && !node->empty() && (*node)[0].is_number())
            return (*node)[0].get<double>();
        if (node->is_number())
            return node->get<double>();
        return fail(kErrLibrary, std::format("path '{}' does not resolve to a number", path));
    };
}

} // namespace qlab::pulse
