#pragma once
// Spec 13 §8 — diagnostic catalogue (QL1xxx lexical, QL2xxx syntax, QL3xxx semantic).
#include "Core/Error.hpp"
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace qlab::lang {

enum class Severity { Error, Warning, Info };

struct DiagInfo {
    std::string_view id;
    Severity severity;
    std::string_view message; // std::format template with {} placeholders
    std::string_view hint;    // fix hint, may be empty
};

struct Diagnostic {
    Severity severity = Severity::Error;
    Error error;     // carries id, span, message, notes
    std::string fix; // optional replacement text
    const std::string& id() const { return error.diagnosticId; }
    bool isError() const { return severity == Severity::Error; }
};

class Diagnostics {
  public:
    // Look up a catalogue entry; unknown ids yield a generic entry.
    static const DiagInfo& info(std::string_view id);
    static const std::vector<DiagInfo>& all();
    // Build a diagnostic from an id, a span and message arguments.
    template <class... Args>
    static Diagnostic make(std::string_view id, SourceSpan span, Args&&... args) {
        const DiagInfo& d = info(id);
        std::string msg = formatMessage(d.message, {toString(std::forward<Args>(args))...});
        Diagnostic diag;
        diag.severity = d.severity;
        diag.error = Error(codeFor(id), msg);
        diag.error.withSpan(std::move(span)).withId(std::string(id));
        if (!d.hint.empty())
            diag.error.withNote(std::string(d.hint));
        return diag;
    }
    static ErrorCode codeFor(std::string_view id);
    static std::string formatMessage(std::string_view tmpl, const std::vector<std::string>& args);
    // Serialises the catalogue as the JSON written to Assets/Lang/diagnostics.json.
    static std::string catalogueJson();

  private:
    static std::string toString(const std::string& s) { return s; }
    static std::string toString(std::string_view s) { return std::string(s); }
    static std::string toString(const char* s) { return s; }
    template <class T> static std::string toString(T v) { return std::format("{}", v); }
};

// Convenience: true if any diagnostic is an error.
bool hasErrors(const std::vector<Diagnostic>& ds);
std::vector<Error> errorsOf(const std::vector<Diagnostic>& ds);

} // namespace qlab::lang
