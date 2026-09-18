#pragma once
// Recursive-descent parser: LaTeX subset → MathNode tree (spec 20 §6). Never throws.
#include "UI/Math/MathAst.hpp"
#include "UI/Math/MathTokenizer.hpp"
#include <string>
#include <vector>

namespace qlab::ui::math {

struct ParseOutput {
    NodePtr root;                        // a Row
    std::vector<std::string> warnings;   // unknown commands, unbalanced groups, ...
    std::vector<std::string> errors;     // structural failures that prevented layout (rare)
};

class MathParser {
public:
    explicit MathParser(std::string_view latex);
    ParseOutput parse();

private:
    const MathToken& peek(std::size_t k = 0) const;
    MathToken next();
    bool at(TokKind k) const { return peek().kind == k; }
    bool atCommand(std::string_view name) const;

    NodePtr parseRow(bool stopAtRBrace, bool stopAtAmpOrNewRow = false, std::string_view endEnv = {});
    NodePtr parseAtom();           // one atom (no trailing scripts)
    NodePtr parseArgument();       // {group} or a single atom
    NodePtr parseOptionalArg(bool& present); // [ ... ]
    NodePtr parseCommand(const MathToken& t);
    NodePtr parseEnvironment(const MathToken& beginTok);
    NodePtr parseLeftRight(const MathToken& leftTok);
    std::string parseDelimiterToken();
    std::string rawGroupText();    // {text} → literal text (for \text, \operatorname)
    NodePtr applyScripts(NodePtr base);
    void warn(std::string msg) { warnings_.push_back(std::move(msg)); }

    std::string_view src_;
    std::vector<MathToken> toks_;
    std::size_t pos_ = 0;
    std::vector<std::string> warnings_;
    std::vector<std::string> errors_;
    int depth_ = 0;
};

} // namespace qlab::ui::math
