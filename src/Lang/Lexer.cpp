#include "Lang/Lexer.hpp"
#include <array>
#include <cmath>

// Spec 13 §1 — keyword tables, token-kind names and the strict/lenient entry points. The scanner
// itself lives in LexerScan.cpp.
namespace qlab::lang {

namespace {
constexpr std::array<std::string_view, 34> kKeywords = {
    "OPENQASM", "include", "qubit", "bit", "gate", "measure", "reset", "barrier", "delay", "if",
    "else", "for", "in", "while", "switch", "case", "default", "def", "return", "extern", "const",
    "input", "output", "let", "box", "cal", "defcal", "pragma", "break", "continue", "end",
    "defcalgrammar", "ctrl", "negctrl"};
constexpr std::array<std::string_view, 12> kTypes = {"int", "uint", "float", "angle", "bool",
    "bit", "duration", "stretch", "complex", "qubit", "qreg", "creg"};
// π, τ and ε are the Unicode spellings of the built-in constants (spec 13 §1 keyword list).
constexpr std::array<std::string_view, 28> kBuiltins = {"pi", "euler", "tau", "\xCF\x80",
    "\xCF\x84", "\xCE\xB5", "sin", "cos", "tan", "arcsin", "arccos", "arctan", "exp", "ln", "sqrt",
    "pow", "mod", "rotl", "rotr", "popcount", "real", "imag", "sizeof", "inv", "gphase", "U",
    "durationof", "im"};
constexpr std::array<std::string_view, 16> kPulse = {"port", "frame", "waveform", "play",
    "set_frequency", "shift_frequency", "set_phase", "shift_phase", "get_frequency", "get_phase",
    "capture", "capture_v2", "newframe", "gaussian", "gaussian_square", "drag"};

template <std::size_t N>
bool contains(const std::array<std::string_view, N>& a, std::string_view s) {
    for (auto x : a)
        if (x == s) return true;
    return false;
}
} // namespace

const char* tokenKindName(TokenKind k) {
    switch (k) {
    case TokenKind::Keyword: return "keyword";
    case TokenKind::Type: return "type";
    case TokenKind::Gate: return "gate";
    case TokenKind::Builtin: return "builtin";
    case TokenKind::Identifier: return "identifier";
    case TokenKind::PhysicalQubit: return "physical-qubit";
    case TokenKind::Number: return "number";
    case TokenKind::Duration: return "duration";
    case TokenKind::String: return "string";
    case TokenKind::BitString: return "bitstring";
    case TokenKind::Comment: return "comment";
    case TokenKind::Pragma: return "pragma";
    case TokenKind::CalBlock: return "cal";
    case TokenKind::Operator: return "operator";
    case TokenKind::Punct: return "punct";
    case TokenKind::Invalid: return "invalid";
    case TokenKind::Eof: return "eof";
    }
    return "?";
}

std::int64_t durationToPs(double value, DurationUnit unit) {
    double scale = 1.0;
    switch (unit) {
    case DurationUnit::Ns: scale = 1e3; break;
    case DurationUnit::Us: scale = 1e6; break;
    case DurationUnit::Ms: scale = 1e9; break;
    case DurationUnit::S: scale = 1e12; break;
    case DurationUnit::Dt: case DurationUnit::None: scale = 1.0; break;
    }
    return static_cast<std::int64_t>(std::llround(value * scale));
}
const char* durationUnitName(DurationUnit u) {
    switch (u) {
    case DurationUnit::Ns: return "ns";
    case DurationUnit::Us: return "us";
    case DurationUnit::Ms: return "ms";
    case DurationUnit::S: return "s";
    case DurationUnit::Dt: return "dt";
    case DurationUnit::None: return "";
    }
    return "";
}

bool isKeyword(std::string_view s) { return contains(kKeywords, s); }
bool isTypeKeyword(std::string_view s) { return contains(kTypes, s); }
bool isBuiltinName(std::string_view s) { return contains(kBuiltins, s); }
bool isPulseKeyword(std::string_view s) { return contains(kPulse, s); }

Result<std::vector<Token>> tokenize(std::string_view text, std::string filename) {
    LexOptions o; o.filename = std::move(filename);
    auto r = lex(text, o);
    for (auto& d : r.diagnostics) if (d.isError()) return std::unexpected(d.error);
    return r.tokens;
}
std::vector<Token> tokenizeLenient(std::string_view text, std::string filename) {
    LexOptions o; o.keepComments = true; o.filename = std::move(filename);
    return lex(text, o).tokens;
}

} // namespace qlab::lang
