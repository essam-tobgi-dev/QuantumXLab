#pragma once
// Spec 13 §1 — lexer. Errors are QL1xxx; lexing never aborts (Invalid tokens are emitted).
#include "Lang/Diagnostics.hpp"
#include "Lang/Token.hpp"

namespace qlab::lang {

struct LexResult {
    std::vector<Token> tokens; // Comment tokens are excluded unless keepComments
    std::vector<Diagnostic> diagnostics;
};

struct LexOptions {
    bool keepComments = false; // editor highlighting wants comments
    std::string filename;
};

LexResult lex(std::string_view text, const LexOptions& opts = {});

// Strict: fails on the first lexical error.
Result<std::vector<Token>> tokenize(std::string_view text, std::string filename = "");
// Lenient: never fails; Invalid tokens and comments are included (for the editor).
std::vector<Token> tokenizeLenient(std::string_view text, std::string filename = "");

bool isKeyword(std::string_view s);
bool isTypeKeyword(std::string_view s);
bool isBuiltinName(std::string_view s);
bool isPulseKeyword(std::string_view s);

} // namespace qlab::lang
