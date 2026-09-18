#include "Lang/Diagnostics.hpp"
#include "Core/Json.hpp"
#include <algorithm>
#include <charconv>

namespace qlab::lang {

namespace {
using S = Severity;
const std::vector<DiagInfo>& table() {
    static const std::vector<DiagInfo> t = {
        // lexical
        {"QL1001", S::Error, "invalid character '{}'", "remove the character or place it inside a comment"},
        {"QL1002", S::Error, "unterminated {}", "add the closing delimiter"},
        {"QL1003", S::Error, "malformed numeric literal '{}'", ""},
        {"QL1004", S::Error, "unknown duration unit '{}'", "use ns, us, ms, s or dt"},
        // syntax
        {"QL2001", S::Error, "missing or misplaced 'OPENQASM 3.0;' header{}", "the first statement must be 'OPENQASM 3.0;'"},
        {"QL2002", S::Error, "include '{}' cannot be resolved; only \"stdgates.inc\" is available", ""},
        {"QL2003", S::Error, "unsupported OPENQASM version '{}'", "use 'OPENQASM 3.0;'"},
        {"QL2010", S::Error, "expected {}, found {}", ""},
        {"QL2011", S::Error, "unexpected token '{}' at start of statement", ""},
        {"QL2012", S::Error, "invalid statement inside gate body: {}", "gate bodies may contain only gate calls and barriers"},
        {"QL2013", S::Error, "invalid pulse statement '{}' inside cal/defcal block", ""},
        {"QL2014", S::Error, "malformed {} pragma: {}", ""},
        {"QL2015", S::Error, "nesting deeper than {} levels", "split the expression or block into smaller parts"},
        {"QL2050", S::Error, "unknown pragma 'qlab.{}'", "see spec 13 §7 for the list of qlab pragmas"},
        {"QL2051", S::Warning, "non-qlab pragma '{}' ignored", ""},
        // semantic
        {"QL3001", S::Error, "qubit '{}' declared after the first quantum statement", "move all qubit declarations before the first gate, measure or reset"},
        {"QL3002", S::Warning, "program contains no quantum statements", ""},
        {"QL3010", S::Error, "undeclared identifier '{}'", ""},
        {"QL3011", S::Error, "redeclaration of '{}'", ""},
        {"QL3012", S::Error, "'{}' shadows a global declaration inside a block", "rename the local variable"},
        {"QL3020", S::Error, "type mismatch: {}", ""},
        {"QL3021", S::Error, "physical qubit '{}' mixed with virtual qubits", "use 'pragma qlab.layout physical' and only $k operands, or only declared qubits"},
        {"QL3022", S::Error, "index {} out of range for '{}' of size {}", ""},
        {"QL3030", S::Warning, "measurement result of '{}' is discarded", "assign it to a bit: 'c = measure q;'"},
        {"QL3040", S::Error, "compile-time constant required for {}", ""},
        {"QL3050", S::Error, "'complex' type is not available to user code", ""},
        {"QL3060", S::Error, "recursive {} '{}'", ""},
        {"QL3061", S::Error, "gate name '{}' shadows a stdgates gate", "choose another name"},
        {"QL3070", S::Error, "pow with non-integer exponent on non-rotation gate '{}'", ""},
        {"QL3080", S::Error, "bit register '{}' used directly as a condition", "compare it: 'if (c == 3)'"},
        {"QL3090", S::Error, "unbounded 'while' loop: condition depends on no measurement and is not foldable", ""},
        {"QL3095", S::Error, "'switch' is not supported yet", ""},
        {"QL3096", S::Error, "'stretch' is not supported yet", ""},
        {"QL3100", S::Error, "external classical functions are not available; use 'def'", ""},
        {"QL3110", S::Warning, "defcal for '{}' defined after first use; earlier calls use the device calibration", ""},
        {"QL3120", S::Error, "pulse amplitude {} exceeds full scale (|amp| must be <= 1)", ""},
        {"QL3121", S::Warning, "duration {} rounded to the device sample period", ""},
        {"QL3130", S::Error, "frame '{}' is not attached to a declared physical qubit", ""},
        {"QL3140", S::Error, "qubit '{}' appears twice in one operand list", ""},
        {"QL3150", S::Error, "gate '{}' expects {} qubit operand(s), got {}", ""},
        {"QL3151", S::Error, "gate '{}' expects {} parameter(s), got {}", ""},
        {"QL3160", S::Error, "classical arithmetic on 'duration' with mismatched units: {}", ""},
        {"QL3170", S::Error, "'{}' is not a gate", ""},
        {"QL3171", S::Error, "'{}' is not a qubit operand", ""},
        {"QL3172", S::Error, "cannot assign to '{}': {}", ""},
        {"QL3173", S::Error, "'return' outside a subroutine", ""},
        {"QL3174", S::Error, "'{}' outside a loop", ""},
        {"QL3175", S::Error, "register operands of different sizes in one gate call", ""},
        {"QL3176", S::Error, "'{}' has no member or index", ""},
        {"QL3177", S::Error, "gate body may not contain '{}'", ""},
        {"QL3178", S::Warning, "input '{}' has no sweep or default", "add 'pragma qlab.sweep' or provide a value at run time"},
        // Reserved for the compiler (14 §11) and the runtime (15 §8): they are raised through
        // Diagnostics::make by those modules, and the editor shows them from this catalogue.
        {"QL4010", S::Error, "'dt' duration used but no device is selected", ""},
        {"QL4011", S::Error, "input '{}' is unbound at compile time", ""},
        {"QL4020", S::Error, "loop expands to {} statements; bound is {}", "raise qlab.unroll_bound or reduce the loop"},
        {"QL4030", S::Error, "circuit violates the coupling map and routing is disabled", ""},
        {"QL4040", S::Error, "pulse-level execution requested above the Lindblad qubit cap ({})", ""},
        {"QL4050", S::Error, "'{}' has {} controls; the ancilla-free decomposition is limited to 8", "use ancilla qubits and ccx ladders"},
        {"QL4060", S::Info, "VF2 layout search exceeded its budget; falling back to the dense layout", ""},
        {"QL4070", S::Error, "gate '{}' cannot be decomposed to the native set of device '{}'", ""},
        {"QL4080", S::Error, "no calibration for '{}' on {}", "recalibrate the device or select another qubit/edge"},
        {"QL4090", S::Error, "schedule lasts {} but the device allows at most {}", ""},
        {"QL5001", S::Error, "input '{}' has no value at run time", ""},
        {"QL5010", S::Error, "the Lindblad backend supports at most {} qubits at {} levels", ""},
        {"QL5011", S::Error, "circuit needs {} qubits; the largest available backend supports {}", ""},
        {"QL5012", S::Error, "memory budget exceeded: {}", "reduce shots, qubits or the backend's precision"},
        {"QL5020", S::Warning, "runtime loop reached its iteration bound ({}) on this shot", ""},
        {"QL5030", S::Error, "sweep grid has {} points; at most {} are allowed", ""},
        {"QL5040", S::Error, "shot count {} is outside [1, 1e7]", ""},
        {"QL5050", S::Error, "custom noise file '{}' is invalid: {}", ""},
        {"QL5060", S::Warning, "run cancelled; the partial result is kept", ""},
    };
    return t;
}
} // namespace

const std::vector<DiagInfo>& Diagnostics::all() { return table(); }

const DiagInfo& Diagnostics::info(std::string_view id) {
    // Fallback for an id that is not catalogued (a programming error: the corpus test in
    // tests/Lang checks that every id the module can emit is listed). The message is the
    // caller's first argument, so nothing is lost.
    static const DiagInfo unknown{"", Severity::Error, "{}", ""};
    for (const auto& d : table())
        if (d.id == id) return d;
    return unknown;
}

ErrorCode Diagnostics::codeFor(std::string_view id) {
    unsigned n = 0;
    if (id.size() == 6 && id.starts_with("QL")) std::from_chars(id.data() + 2, id.data() + 6, n);
    switch (n / 1000) {
    case 1: case 2: case 3: return ErrorCode::Lang_ + (n % 1000);
    case 4: return ErrorCode::Compiler_ + (n % 1000);
    case 5: return ErrorCode::Runtime_ + (n % 1000);
    default: return ErrorCode::Parse;
    }
}

std::string Diagnostics::formatMessage(std::string_view tmpl, const std::vector<std::string>& args) {
    std::string out;
    std::size_t ai = 0;
    for (std::size_t i = 0; i < tmpl.size(); ++i) {
        if (tmpl[i] == '{' && i + 1 < tmpl.size() && tmpl[i + 1] == '}') {
            if (ai < args.size()) out += args[ai];
            ++ai; ++i;
        } else out += tmpl[i];
    }
    // Append unused args (defensive: templates with fewer placeholders than args).
    for (; ai < args.size(); ++ai) { if (!args[ai].empty()) { out += ' '; out += args[ai]; } }
    return out;
}

namespace {
const char* severityName(Severity s) {
    switch (s) {
    case Severity::Error: return "error";
    case Severity::Warning: return "warning";
    case Severity::Info: return "info";
    }
    return "error";
}
} // namespace

std::string Diagnostics::catalogueJson() {
    core::Json data = core::Json::array();
    for (const auto& d : table())
        data.push_back({{"id", std::string(d.id)},
                        {"severity", severityName(d.severity)},
                        {"message", std::string(d.message)},
                        {"hint", std::string(d.hint)}});
    return core::JsonEnvelope::serialize("lang.diagnostics", data) + "\n";
}

bool hasErrors(const std::vector<Diagnostic>& ds) {
    return std::any_of(ds.begin(), ds.end(), [](const Diagnostic& d) { return d.isError(); });
}
std::vector<Error> errorsOf(const std::vector<Diagnostic>& ds) {
    std::vector<Error> out;
    for (const auto& d : ds) if (d.isError()) out.push_back(d.error);
    return out;
}

} // namespace qlab::lang
