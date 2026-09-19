#pragma once
// Spec 13 §2 — recursive-descent parser with statement-level error recovery.
#include "Lang/Ast.hpp"
#include "Lang/Diagnostics.hpp"
#include "Lang/Lexer.hpp"

namespace qlab::lang {

struct ParseResult {
    Ast ast;
    std::vector<Diagnostic> diagnostics; // lexical + syntax, in source order
};

// Parses a whole program. Never throws; syntax errors are recorded and parsing resumes at the
// next statement boundary so several errors can be reported at once.
ParseResult parse(std::string_view text, std::string filename = "");

class Parser {
  public:
    Parser(std::vector<Token> tokens, std::string filename);
    Ast parseProgram();
    std::vector<Diagnostic> takeDiagnostics() { return std::move(diags_); }

    // Exposed for tests / sub-parsers
    ExprPtr parseExpr();
    // Never returns null: on a failed parse it reports QL2010 (once) and yields an ErrorExpr,
    // so no downstream consumer can dereference a null expression.
    ExprPtr parseExprOrError();

  private:
    // ---- token stream
    const Token& peek(std::size_t k = 0) const;
    const Token& advance();
    bool atEnd() const { return peek().kind == TokenKind::Eof; }
    bool check(TokenKind k) const { return peek().kind == k; }
    bool checkText(std::string_view t) const;
    bool checkKw(std::string_view t) const {
        return peek().kind == TokenKind::Keyword && peek().text == t;
    }
    bool checkPunct(char c) const {
        return peek().kind == TokenKind::Punct && peek().text.size() == 1 && peek().text[0] == c;
    }
    bool checkOp(std::string_view t) const {
        return peek().kind == TokenKind::Operator && peek().text == t;
    }
    bool match(TokenKind k);
    bool matchText(std::string_view t);
    bool matchPunct(char c);
    bool matchOp(std::string_view t);
    // Reports QL2010 and returns false if the expected token is absent.
    bool expectPunct(char c, std::string_view what = "");
    bool expectOp(std::string_view t);
    bool expectKw(std::string_view t);
    std::optional<std::string> expectIdent(std::string_view what = "identifier");
    SourceSpan spanFrom(const SourceSpan& start) const;
    SourceSpan here() const { return peek().span; }

    // ---- diagnostics & recovery
    // Panic mode (spec 13 §8): the first diagnostic of a statement suppresses every further one
    // until the next statement starts, so one malformed construct yields one diagnostic.
    template <class... A> void error(std::string_view id, SourceSpan sp, A&&... a) {
        if (panic_)
            return;
        diags_.push_back(Diagnostics::make(id, std::move(sp), std::forward<A>(a)...));
        ++errorCount_;
        panic_ = true;
    }
    void expected(std::string_view what);
    // Skip to just after the next ';' at bracket depth 0, or stop before a '}' closing the
    // enclosing block, a pragma, or a statement-starting token on a later line.
    void synchronize();

    // ---- statements (ParserCore / ParserDecl / ParserStmt)
    StmtPtr parseStatement();
    StmtList parseBlock(); // '{' ... '}'
    StmtList parseBlockOrStmt();
    StmtPtr parseVersion();
    StmtPtr parseInclude();
    StmtPtr parsePragma();
    StmtPtr parseQubitDecl();
    StmtPtr parseClassicalDecl(IoKind io);
    StmtPtr parseGateDef();
    StmtPtr parseDef();
    StmtPtr parseExtern();
    StmtPtr parseGateCallOrAssign();
    StmtPtr parseGateCall(std::vector<GateModifier> mods, SourceSpan start);
    StmtPtr parseMeasure();
    StmtPtr parseReset();
    StmtPtr parseBarrier();
    StmtPtr parseDelay();
    StmtPtr parseBox();
    StmtPtr parseIf();
    StmtPtr parseFor();
    StmtPtr parseWhile();
    StmtPtr parseSwitch();
    StmtPtr parseReturn();
    StmtPtr parseCal();
    StmtPtr parseDefcal();
    bool parseTypeSpec(TypeSpec& out); // scalar or bit[n]/qubit[n]
    std::vector<ExprPtr> parseOperandList();
    ExprPtr parseOperand();
    std::vector<std::string> parseIdList();
    std::optional<ExprPtr> parseDesignator(); // '[' expr ']'
    std::vector<GateModifier> parseModifiers();

    // ---- expressions (ParserExpr)
    ExprPtr parseBinary(int minPrec);
    ExprPtr parseUnary();
    ExprPtr parsePostfix(ExprPtr base);
    ExprPtr parsePrimary();
    ExprPtr parseSetLiteral();
    ExprPtr parseIndexOrRange(ExprPtr base);
    std::vector<ExprPtr> parseExprList(char close);

    // ---- pulse (ParserPulse)
    std::vector<PulseStmt> parsePulseBody();
    bool parsePulseStmt(std::vector<PulseStmt>& out);
    std::optional<PulseWaveform> parseWaveformExpr();

    template <class T> StmtPtr mk(T node, SourceSpan sp) {
        auto s = std::make_unique<Stmt>();
        s->node = std::move(node);
        s->span = std::move(sp);
        return s;
    }
    template <class T> ExprPtr mkE(T node, SourceSpan sp) {
        auto e = std::make_unique<Expr>();
        e->node = std::move(node);
        e->span = std::move(sp);
        return e;
    }

    std::vector<Token> toks_;
    std::size_t pos_ = 0;
    std::string filename_;
    std::vector<Diagnostic> diags_;
    int errorCount_ = 0;
    int depth_ = 0; // brace depth
    bool inGateBody_ = false;
    bool sawVersion_ = false;
    bool panic_ = false; // a diagnostic was reported (or an Invalid token met) in this statement
    int nesting_ = 0;    // statement + expression recursion depth, bounded by kMaxNesting
};

} // namespace qlab::lang
