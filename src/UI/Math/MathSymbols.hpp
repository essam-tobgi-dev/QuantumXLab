#pragma once
// Symbol tables for the LaTeX subset (spec 20 §3 coverage list).
#include "UI/Math/MathAst.hpp"
#include <optional>
#include <string_view>

namespace qlab::ui::math {

struct SymbolInfo {
    std::string_view glyph; // UTF-8
    AtomClass cls;
};

// \alpha, \hbar, \pm, \le, ... → glyph + class. Empty optional if unknown.
std::optional<SymbolInfo> lookupSymbol(std::string_view command);

// Function-name operators (\sin, \cos, \Tr, ...) rendered upright.
bool isFunctionOperator(std::string_view command);

// Large operators (\sum, \int, ...) → glyph; `integral` marks ones whose limits stay as scripts.
struct BigOpInfo { std::string_view glyph; bool integral; };
std::optional<BigOpInfo> lookupBigOp(std::string_view command);

// Delimiters accepted after \left / \right (and bare | ‖ etc.).
std::optional<std::string_view> lookupDelimiter(std::string_view token);

// Accents: \vec \hat \bar \tilde \dot \ddot \overline \underline \underbrace.
struct AccentInfo { std::string_view glyph; bool wide; bool below; };
std::optional<AccentInfo> lookupAccent(std::string_view command);

// Style commands: \mathbf \mathrm \mathcal \mathbb \mathit \boldsymbol \text \operatorname \textrm
bool isStyleCommand(std::string_view command);

// Spacing commands → width in em (\, \; \: \! \quad \qquad \  \enspace)
std::optional<double> lookupSpace(std::string_view command);

// Map a codepoint / ASCII char to its atom class.
AtomClass classifyChar(char32_t cp);

// Apply a math alphabet (bb, cal, bf) to a single ASCII letter/digit; returns UTF-8.
std::string styledLetter(std::string_view styleName, char c);

} // namespace qlab::ui::math
