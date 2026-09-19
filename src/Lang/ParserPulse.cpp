#include "Lang/Parser.hpp"
#include "Lang/ParserUtil.hpp"

namespace qlab::lang {

std::optional<PulseWaveform> Parser::parseWaveformExpr() {
    PulseWaveform w;
    if (checkPunct('[')) { // literal sample list
        advance();
        w.kind = "samples";
        w.args = parseExprList(']');
        expectPunct(']');
        return w;
    }
    auto id = expectIdent("waveform");
    if (!id)
        return std::nullopt;
    static const char* kinds[] = {"gaussian", "gaussian_square", "drag", "constant", "sine",
                                  "sech",     "cosine"};
    bool known = false;
    for (auto k : kinds)
        if (*id == k)
            known = true;
    if (known && checkPunct('(')) {
        advance();
        w.kind = *id;
        w.args = parseExprList(')');
        expectPunct(')');
        std::size_t want = *id == "gaussian"          ? 3
                           : *id == "gaussian_square" ? 4
                           : *id == "drag"            ? 4
                           : *id == "constant"        ? 2
                           : *id == "sine"            ? 4
                                                      : 3;
        if (w.args.size() != want)
            error("QL2010", here(), std::to_string(want) + " waveform arguments",
                  std::to_string(w.args.size()));
        return w;
    }
    w.kind = "ref"; // reference to a declared waveform
    w.args.clear();
    w.args.push_back(mkE(Ident{*id}, here()));
    return w;
}

// One pulse statement (spec 13 §6). Same recovery contract as parseStatement: a statement with a
// syntax error is reported once, dropped, and skipped to its end.
bool Parser::parsePulseStmt(std::vector<PulseStmt>& out) {
    panic_ = false;
    const std::size_t start = pos_, count = out.size();
    SourceSpan sp = here();
    PulseStmt ps;
    ps.span = sp;
    const Token& t = peek();
    auto finish = [&]() {
        ps.span = spanFrom(sp);
        out.push_back(std::move(ps));
        return true;
    };
    auto parseOne = [&]() -> bool {
        if (t.text == "extern") { // extern port name;
            advance();
            if (!matchText("port")) {
                expected("'port'");
                synchronize();
                return false;
            }
            auto n = expectIdent("port name");
            expectPunct(';');
            ps.node = PulsePortDecl{n.value_or("?")};
            return finish();
        }
        if (t.text == "port") {
            advance();
            auto n = expectIdent("port name");
            expectPunct(';');
            ps.node = PulsePortDecl{n.value_or("?")};
            return finish();
        }
        if (t.text == "frame") {
            advance();
            auto n = expectIdent("frame name");
            expectOp("=");
            if (!matchText("newframe")) {
                expected("'newframe'");
                synchronize();
                return false;
            }
            expectPunct('(');
            auto port = expectIdent("port");
            expectPunct(',');
            PulseFrameDecl f;
            f.name = n.value_or("?");
            f.port = port.value_or("?");
            f.frequency = parseExprOrError();
            expectPunct(',');
            f.phase = parseExprOrError();
            expectPunct(')');
            expectPunct(';');
            ps.node = std::move(f);
            return finish();
        }
        if (t.text == "waveform") {
            advance();
            auto n = expectIdent("waveform name");
            expectOp("=");
            auto w = parseWaveformExpr();
            expectPunct(';');
            if (!w)
                return false;
            ps.node = PulseWaveformDecl{n.value_or("?"), std::move(*w)};
            return finish();
        }
        if (t.text == "play") {
            advance();
            expectPunct('(');
            auto fr = expectIdent("frame");
            expectPunct(',');
            PulsePlay p;
            p.frame = fr.value_or("?");
            auto w = parseWaveformExpr();
            if (!w)
                return false;
            if (w->kind == "ref") {
                auto* id = w->args.front()->as<Ident>();
                p.waveform = id ? id->name : std::string("?");
            } else
                p.waveform = std::move(*w);
            expectPunct(')');
            expectPunct(';');
            ps.node = std::move(p);
            return finish();
        }
        if (t.text == "set_frequency" || t.text == "shift_frequency" || t.text == "set_phase" ||
            t.text == "shift_phase") {
            std::string op = advance().text;
            expectPunct('(');
            auto fr = expectIdent("frame");
            expectPunct(',');
            PulseFrameOp f;
            f.op = op;
            f.frame = fr.value_or("?");
            f.value = parseExprOrError();
            expectPunct(')');
            expectPunct(';');
            ps.node = std::move(f);
            return finish();
        }
        if (t.text == "delay") {
            advance();
            PulseDelay d;
            expectPunct('[');
            d.duration = parseExprOrError();
            expectPunct(']');
            if (!checkPunct(';'))
                d.frames = parseIdList();
            expectPunct(';');
            ps.node = std::move(d);
            return finish();
        }
        if (t.text == "barrier") {
            advance();
            PulseBarrier b;
            if (!checkPunct(';'))
                b.frames = parseIdList();
            expectPunct(';');
            ps.node = std::move(b);
            return finish();
        }
        if (t.text == "capture_v2" || t.text == "capture") {
            advance();
            expectPunct('(');
            auto fr = expectIdent("frame");
            expectPunct(',');
            PulseCapture c;
            c.frame = fr.value_or("?");
            c.duration = parseExprOrError();
            expectPunct(')');
            expectPunct(';');
            ps.node = std::move(c);
            return finish();
        }
        // lvalue = capture_v2(frame, dur);
        if ((t.kind == TokenKind::Identifier) && peek(1).kind == TokenKind::Operator &&
            peek(1).text == "=" && (peek(2).text == "capture_v2" || peek(2).text == "capture")) {
            std::string target = advance().text;
            advance();
            advance();
            expectPunct('(');
            auto fr = expectIdent("frame");
            expectPunct(',');
            PulseCapture c;
            c.frame = fr.value_or("?");
            c.duration = parseExprOrError();
            c.target = target;
            expectPunct(')');
            expectPunct(';');
            ps.node = std::move(c);
            return finish();
        }
        if (t.text == "return" && (peek(1).text == "capture_v2" ||
                                   peek(1).text == "capture")) { // `return capture_v2(...)`
            advance();
            advance();
            expectPunct('(');
            auto fr = expectIdent("frame");
            expectPunct(',');
            PulseCapture c;
            c.frame = fr.value_or("?");
            c.duration = parseExprOrError();
            c.target = std::string("return");
            expectPunct(')');
            expectPunct(';');
            ps.node = std::move(c);
            return finish();
        }
        error("QL2013", sp, t.kind == TokenKind::Eof ? std::string("end of file") : t.text);
        if (!atEnd() && !checkPunct('{'))
            advance(); // the offending token starts no statement
        synchronize();
        return false;
    };
    bool ok = parseOne();
    if (!panic_)
        return ok;
    out.erase(out.begin() + static_cast<std::ptrdiff_t>(count), out.end());
    const Token& last = toks_[pos_ > 0 ? pos_ - 1 : 0];
    if (!(pos_ > start && (detail::isPunct(last, ';') || detail::isPunct(last, '}'))))
        synchronize();
    return false;
}

std::vector<PulseStmt> Parser::parsePulseBody() {
    std::vector<PulseStmt> body;
    if (!expectPunct('{')) {
        synchronize();
        return body;
    }
    ++depth_;
    while (!atEnd() && !checkPunct('}')) {
        std::size_t before = pos_;
        if (checkPunct(';')) {
            advance();
            continue;
        }
        parsePulseStmt(body);
        if (pos_ == before && !atEnd())
            advance();
    }
    --depth_;
    panic_ = false; // the closing brace belongs to the enclosing cal/defcal statement
    expectPunct('}');
    return body;
}

StmtPtr Parser::parseCal() {
    SourceSpan sp = advance().span;
    CalStmt c;
    c.body = parsePulseBody();
    return mk(std::move(c), spanFrom(sp));
}

StmtPtr Parser::parseDefcal() {
    SourceSpan sp = advance().span;
    DefcalStmt d;
    if (checkKw("measure") || checkKw("reset") || checkKw("delay"))
        d.gate = advance().text;
    else {
        auto g = expectIdent("gate name");
        if (!g) {
            synchronize();
            return nullptr;
        }
        d.gate = *g;
    }
    if (matchPunct('(')) {
        while (!checkPunct(')') && !atEnd()) {
            if (check(TokenKind::Type)) {
                advance();
                auto n = expectIdent("parameter name");
                d.paramNames.push_back(n.value_or("?"));
                d.params.push_back(nullptr);
            } else {
                ExprPtr e = parseExpr();
                if (!e)
                    break;
                d.params.push_back(std::move(e));
                d.paramNames.push_back("");
            }
            if (!matchPunct(','))
                break;
        }
        expectPunct(')');
    }
    do {
        if (!check(TokenKind::PhysicalQubit)) {
            expected("physical qubit");
            break;
        }
        d.qubits.push_back(PhysicalQubitRef{static_cast<std::uint32_t>(advance().ival)});
    } while (matchPunct(','));
    if (matchOp("->"))
        parseTypeSpec(d.returnType);
    d.body = parsePulseBody();
    return mk(std::move(d), spanFrom(sp));
}

} // namespace qlab::lang
