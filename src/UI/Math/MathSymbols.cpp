#include "UI/Math/MathSymbols.hpp"
#include <string>
#include <unordered_map>

namespace qlab::ui::math {
namespace {
using SV = std::string_view;
const std::unordered_map<SV, SymbolInfo>& symbolTable() {
    static const std::unordered_map<SV, SymbolInfo> t = {
        // Greek lower
        {"alpha", {"α", AtomClass::Ord}},
        {"beta", {"β", AtomClass::Ord}},
        {"gamma", {"γ", AtomClass::Ord}},
        {"delta", {"δ", AtomClass::Ord}},
        {"epsilon", {"ϵ", AtomClass::Ord}},
        {"varepsilon", {"ε", AtomClass::Ord}},
        {"zeta", {"ζ", AtomClass::Ord}},
        {"eta", {"η", AtomClass::Ord}},
        {"theta", {"θ", AtomClass::Ord}},
        {"vartheta", {"ϑ", AtomClass::Ord}},
        {"iota", {"ι", AtomClass::Ord}},
        {"kappa", {"κ", AtomClass::Ord}},
        {"lambda", {"λ", AtomClass::Ord}},
        {"mu", {"μ", AtomClass::Ord}},
        {"nu", {"ν", AtomClass::Ord}},
        {"xi", {"ξ", AtomClass::Ord}},
        {"pi", {"π", AtomClass::Ord}},
        {"varpi", {"ϖ", AtomClass::Ord}},
        {"rho", {"ρ", AtomClass::Ord}},
        {"varrho", {"ϱ", AtomClass::Ord}},
        {"sigma", {"σ", AtomClass::Ord}},
        {"varsigma", {"ς", AtomClass::Ord}},
        {"tau", {"τ", AtomClass::Ord}},
        {"upsilon", {"υ", AtomClass::Ord}},
        {"phi", {"ϕ", AtomClass::Ord}},
        {"varphi", {"φ", AtomClass::Ord}},
        {"chi", {"χ", AtomClass::Ord}},
        {"psi", {"ψ", AtomClass::Ord}},
        {"omega", {"ω", AtomClass::Ord}},
        // Greek upper
        {"Gamma", {"Γ", AtomClass::Ord}},
        {"Delta", {"Δ", AtomClass::Ord}},
        {"Theta", {"Θ", AtomClass::Ord}},
        {"Lambda", {"Λ", AtomClass::Ord}},
        {"Xi", {"Ξ", AtomClass::Ord}},
        {"Pi", {"Π", AtomClass::Ord}},
        {"Sigma", {"Σ", AtomClass::Ord}},
        {"Upsilon", {"Υ", AtomClass::Ord}},
        {"Phi", {"Φ", AtomClass::Ord}},
        {"Psi", {"Ψ", AtomClass::Ord}},
        {"Omega", {"Ω", AtomClass::Ord}},
        // Ordinary symbols
        {"hbar", {"ℏ", AtomClass::Ord}},
        {"partial", {"∂", AtomClass::Ord}},
        {"nabla", {"∇", AtomClass::Ord}},
        {"infty", {"∞", AtomClass::Ord}},
        {"dagger", {"†", AtomClass::Ord}},
        {"ddagger", {"‡", AtomClass::Ord}},
        {"ldots", {"…", AtomClass::Inner}},
        {"cdots", {"⋯", AtomClass::Inner}},
        {"vdots", {"⋮", AtomClass::Inner}},
        {"ddots", {"⋱", AtomClass::Inner}},
        {"dots", {"…", AtomClass::Inner}},
        {"prime", {"′", AtomClass::Ord}},
        {"ell", {"ℓ", AtomClass::Ord}},
        {"imath", {"ı", AtomClass::Ord}},
        {"jmath", {"ȷ", AtomClass::Ord}},
        {"emptyset", {"∅", AtomClass::Ord}},
        {"varnothing", {"∅", AtomClass::Ord}},
        {"forall", {"∀", AtomClass::Ord}},
        {"exists", {"∃", AtomClass::Ord}},
        {"neg", {"¬", AtomClass::Ord}},
        {"lnot", {"¬", AtomClass::Ord}},
        {"aleph", {"ℵ", AtomClass::Ord}},
        {"Re", {"ℜ", AtomClass::Ord}},
        {"Im", {"ℑ", AtomClass::Ord}},
        {"angle", {"∠", AtomClass::Ord}},
        {"top", {"⊤", AtomClass::Ord}},
        {"bot", {"⊥", AtomClass::Ord}},
        {"degree", {"°", AtomClass::Ord}},
        {"star", {"⋆", AtomClass::Bin}},
        {"ast", {"∗", AtomClass::Bin}},
        {"bullet", {"•", AtomClass::Bin}},
        {"perp", {"⊥", AtomClass::Rel}},
        {"parallel", {"∥", AtomClass::Rel}},
        // Binary operators
        {"pm", {"±", AtomClass::Bin}},
        {"mp", {"∓", AtomClass::Bin}},
        {"cdot", {"⋅", AtomClass::Bin}},
        {"times", {"×", AtomClass::Bin}},
        {"div", {"÷", AtomClass::Bin}},
        {"circ", {"∘", AtomClass::Bin}},
        {"otimes", {"⊗", AtomClass::Bin}},
        {"oplus", {"⊕", AtomClass::Bin}},
        {"ominus", {"⊖", AtomClass::Bin}},
        {"odot", {"⊙", AtomClass::Bin}},
        {"cup", {"∪", AtomClass::Bin}},
        {"cap", {"∩", AtomClass::Bin}},
        {"setminus", {"∖", AtomClass::Bin}},
        {"wedge", {"∧", AtomClass::Bin}},
        {"vee", {"∨", AtomClass::Bin}},
        {"land", {"∧", AtomClass::Bin}},
        {"lor", {"∨", AtomClass::Bin}},
        {"sqcup", {"⊔", AtomClass::Bin}},
        // Relations
        {"to", {"→", AtomClass::Rel}},
        {"rightarrow", {"→", AtomClass::Rel}},
        {"leftarrow", {"←", AtomClass::Rel}},
        {"leftrightarrow", {"↔", AtomClass::Rel}},
        {"Rightarrow", {"⇒", AtomClass::Rel}},
        {"Leftarrow", {"⇐", AtomClass::Rel}},
        {"Leftrightarrow", {"⇔", AtomClass::Rel}},
        {"mapsto", {"↦", AtomClass::Rel}},
        {"longrightarrow", {"⟶", AtomClass::Rel}},
        {"Longrightarrow", {"⟹", AtomClass::Rel}},
        {"Longleftarrow", {"⟸", AtomClass::Rel}},
        {"Longleftrightarrow", {"⟺", AtomClass::Rel}},
        {"longleftarrow", {"⟵", AtomClass::Rel}},
        {"prec", {"≺", AtomClass::Rel}},
        {"succ", {"≻", AtomClass::Rel}},
        {"preceq", {"⪯", AtomClass::Rel}},
        {"succeq", {"⪰", AtomClass::Rel}},
        {"diamond", {"⋄", AtomClass::Bin}},
        {"star", {"⋆", AtomClass::Bin}},
        {"longleftrightarrow", {"⟷", AtomClass::Rel}},
        {"uparrow", {"↑", AtomClass::Rel}},
        {"downarrow", {"↓", AtomClass::Rel}},
        {"nearrow", {"↗", AtomClass::Rel}},
        {"searrow", {"↘", AtomClass::Rel}},
        {"approx", {"≈", AtomClass::Rel}},
        {"simeq", {"≃", AtomClass::Rel}},
        {"sim", {"∼", AtomClass::Rel}},
        {"cong", {"≅", AtomClass::Rel}},
        {"propto", {"∝", AtomClass::Rel}},
        {"ll", {"≪", AtomClass::Rel}},
        {"gg", {"≫", AtomClass::Rel}},
        {"le", {"≤", AtomClass::Rel}},
        {"leq", {"≤", AtomClass::Rel}},
        {"ge", {"≥", AtomClass::Rel}},
        {"geq", {"≥", AtomClass::Rel}},
        {"ne", {"≠", AtomClass::Rel}},
        {"neq", {"≠", AtomClass::Rel}},
        {"equiv", {"≡", AtomClass::Rel}},
        {"in", {"∈", AtomClass::Rel}},
        {"notin", {"∉", AtomClass::Rel}},
        {"ni", {"∋", AtomClass::Rel}},
        {"subset", {"⊂", AtomClass::Rel}},
        {"subseteq", {"⊆", AtomClass::Rel}},
        {"supset", {"⊃", AtomClass::Rel}},
        {"supseteq", {"⊇", AtomClass::Rel}},
        {"lesssim", {"≲", AtomClass::Rel}},
        {"gtrsim", {"≳", AtomClass::Rel}},
        {"leqslant", {"⩽", AtomClass::Rel}},
        {"geqslant", {"⩾", AtomClass::Rel}},
        {"mid", {"|", AtomClass::Rel}},
        {"doteq", {"≐", AtomClass::Rel}},
        {"triangleq", {"≜", AtomClass::Rel}},
        {"coloneqq", {"≔", AtomClass::Rel}},
        {"models", {"⊨", AtomClass::Rel}},
        {"vdash", {"⊢", AtomClass::Rel}},
        {"iff", {"⟺", AtomClass::Rel}},
        {"implies", {"⟹", AtomClass::Rel}},
        // Delimiters as ordinary symbols when not paired
        {"langle", {"⟨", AtomClass::Open}},
        {"rangle", {"⟩", AtomClass::Close}},
        {"lvert", {"|", AtomClass::Open}},
        {"rvert", {"|", AtomClass::Close}},
        {"lVert", {"‖", AtomClass::Open}},
        {"rVert", {"‖", AtomClass::Close}},
        {"|", {"‖", AtomClass::Ord}},
        {"lfloor", {"⌊", AtomClass::Open}},
        {"rfloor", {"⌋", AtomClass::Close}},
        {"lceil", {"⌈", AtomClass::Open}},
        {"rceil", {"⌉", AtomClass::Close}},
        {"lbrace", {"{", AtomClass::Open}},
        {"rbrace", {"}", AtomClass::Close}},
        {"{", {"{", AtomClass::Open}},
        {"}", {"}", AtomClass::Close}},
        {"backslash", {"\\", AtomClass::Ord}},
        {"%", {"%", AtomClass::Ord}},
        {"&", {"&", AtomClass::Ord}},
        {"#", {"#", AtomClass::Ord}},
        {"_", {"_", AtomClass::Ord}},
        {"$", {"$", AtomClass::Ord}},
        {"colon", {":", AtomClass::Punct}},
        {"cdotp", {"·", AtomClass::Punct}},
    };
    return t;
}
} // namespace

std::optional<SymbolInfo> lookupSymbol(std::string_view command) {
    auto it = symbolTable().find(command);
    if (it == symbolTable().end())
        return std::nullopt;
    return it->second;
}

bool isFunctionOperator(std::string_view c) {
    static const std::unordered_map<SV, bool> t = {
        {"sin", 1},    {"cos", 1},    {"tan", 1},    {"cot", 1},  {"sec", 1},    {"csc", 1},
        {"arcsin", 1}, {"arccos", 1}, {"arctan", 1}, {"sinh", 1}, {"cosh", 1},   {"tanh", 1},
        {"exp", 1},    {"ln", 1},     {"log", 1},    {"lg", 1},   {"arg", 1},    {"Tr", 1},
        {"tr", 1},     {"det", 1},    {"erfc", 1},   {"erf", 1},  {"dim", 1},    {"ker", 1},
        {"deg", 1},    {"gcd", 1},    {"sgn", 1},    {"mod", 1},  {"Pr", 1},     {"sup", 1},
        {"inf", 1},    {"lim", 1},    {"max", 1},    {"min", 1},  {"argmax", 1}, {"argmin", 1},
        {"coth", 1},   {"sech", 1},   {"csch", 1},   {"Re", 1},   {"Im", 1},     {"vec", 0}};
    auto it = t.find(c);
    return it != t.end() && it->second;
}

std::optional<BigOpInfo> lookupBigOp(std::string_view c) {
    static const std::unordered_map<SV, BigOpInfo> t = {
        {"sum", {"∑", false}},      {"prod", {"∏", false}},      {"coprod", {"∐", false}},
        {"int", {"∫", true}},       {"iint", {"∬", true}},       {"iiint", {"∭", true}},
        {"oint", {"∮", true}},      {"bigcup", {"⋃", false}},    {"bigcap", {"⋂", false}},
        {"bigoplus", {"⨁", false}}, {"bigotimes", {"⨂", false}}, {"bigwedge", {"⋀", false}},
        {"bigvee", {"⋁", false}}};
    auto it = t.find(c);
    if (it == t.end())
        return std::nullopt;
    return it->second;
}

std::optional<std::string_view> lookupDelimiter(std::string_view tok) {
    static const std::unordered_map<SV, SV> t = {{"(", "("},
                                                 {")", ")"},
                                                 {"[", "["},
                                                 {"]", "]"},
                                                 {"\\{", "{"},
                                                 {"\\}", "}"},
                                                 {"{", "{"},
                                                 {"}", "}"},
                                                 {"|", "|"},
                                                 {"\\|", "‖"},
                                                 {".", ""},
                                                 {"\\langle", "⟨"},
                                                 {"\\rangle", "⟩"},
                                                 {"\\lvert", "|"},
                                                 {"\\rvert", "|"},
                                                 {"\\lVert", "‖"},
                                                 {"\\rVert", "‖"},
                                                 {"\\lfloor", "⌊"},
                                                 {"\\rfloor", "⌋"},
                                                 {"\\lceil", "⌈"},
                                                 {"\\rceil", "⌉"},
                                                 {"\\lbrace", "{"},
                                                 {"\\rbrace", "}"},
                                                 {"/", "/"},
                                                 {"\\backslash", "\\"},
                                                 {"\\uparrow", "↑"},
                                                 {"\\downarrow", "↓"}};
    auto it = t.find(tok);
    if (it == t.end())
        return std::nullopt;
    return it->second;
}

std::optional<AccentInfo> lookupAccent(std::string_view c) {
    static const std::unordered_map<SV, AccentInfo> t = {
        {"vec", {"⃗", false, false}},           {"hat", {"^", false, false}},
        {"widehat", {"^", true, false}},       {"bar", {"¯", false, false}},
        {"tilde", {"~", false, false}},        {"widetilde", {"~", true, false}},
        {"dot", {"˙", false, false}},          {"ddot", {"¨", false, false}},
        {"overline", {"¯", true, false}},      {"underline", {"_", true, true}},
        {"underbrace", {"⏟", true, true}},     {"overbrace", {"⏞", true, false}},
        {"breve", {"˘", false, false}},        {"check", {"ˇ", false, false}},
        {"acute", {"´", false, false}},        {"grave", {"`", false, false}},
        {"overrightarrow", {"→", true, false}}};
    auto it = t.find(c);
    if (it == t.end())
        return std::nullopt;
    return it->second;
}

bool isStyleCommand(std::string_view c) {
    return c == "mathbf" || c == "mathrm" || c == "mathcal" || c == "mathbb" || c == "mathit" ||
           c == "boldsymbol" || c == "text" || c == "operatorname" || c == "textrm" ||
           c == "mathsf" || c == "mathtt" || c == "textbf" || c == "textit" || c == "mathfrak" ||
           c == "mathscr";
}

std::optional<double> lookupSpace(std::string_view c) {
    static const std::unordered_map<SV, double> t = {{",", 3.0 / 18.0},
                                                     {":", 4.0 / 18.0},
                                                     {";", 5.0 / 18.0},
                                                     {"!", -3.0 / 18.0},
                                                     {"quad", 1.0},
                                                     {"qquad", 2.0},
                                                     {" ", 0.33},
                                                     {"enspace", 0.5},
                                                     {"thinspace", 3.0 / 18.0},
                                                     {"medspace", 4.0 / 18.0},
                                                     {"thickspace", 5.0 / 18.0},
                                                     {"negthinspace", -3.0 / 18.0},
                                                     {"mkern", 0.0},
                                                     {"hspace", 0.5}};
    auto it = t.find(c);
    if (it == t.end())
        return std::nullopt;
    return it->second;
}

AtomClass classifyChar(char32_t cp) {
    switch (cp) {
    case '+':
    case '-':
    case '*':
    case U'−':
    case U'·':
        return AtomClass::Bin;
    case '=':
    case '<':
    case '>':
    case ':':
        return AtomClass::Rel;
    case '(':
    case '[':
        return AtomClass::Open;
    case ')':
    case ']':
        return AtomClass::Close;
    case ',':
    case ';':
        return AtomClass::Punct;
    case '/':
        return AtomClass::Ord;
    default:
        return AtomClass::Ord;
    }
}

std::string styledLetter(std::string_view styleName, char c) {
    auto enc = [](char32_t cp) {
        std::string s;
        if (cp < 0x80)
            s += static_cast<char>(cp);
        else if (cp < 0x800) {
            s += static_cast<char>(0xC0 | (cp >> 6));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            s += static_cast<char>(0xE0 | (cp >> 12));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            s += static_cast<char>(0xF0 | (cp >> 18));
            s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        }
        return s;
    };
    if (styleName == "mathbb") {
        static const std::unordered_map<char, char32_t> special = {
            {'C', U'ℂ'}, {'H', U'ℍ'}, {'N', U'ℕ'}, {'P', U'ℙ'},
            {'Q', U'ℚ'}, {'R', U'ℝ'}, {'Z', U'ℤ'}};
        if (auto it = special.find(c); it != special.end())
            return enc(it->second);
        if (c >= 'A' && c <= 'Z')
            return enc(0x1D538 + static_cast<char32_t>(c - 'A'));
        if (c >= 'a' && c <= 'z')
            return enc(0x1D552 + static_cast<char32_t>(c - 'a'));
        if (c >= '0' && c <= '9')
            return enc(0x1D7D8 + static_cast<char32_t>(c - '0'));
    } else if (styleName == "mathcal" || styleName == "mathscr") {
        static const std::unordered_map<char, char32_t> special = {
            {'B', U'ℬ'}, {'E', U'ℰ'}, {'F', U'ℱ'}, {'H', U'ℋ'}, {'I', U'ℐ'},
            {'L', U'ℒ'}, {'M', U'ℳ'}, {'R', U'ℛ'}, {'g', U'ℊ'}, {'o', U'ℴ'}};
        if (auto it = special.find(c); it != special.end())
            return enc(it->second);
        if (c >= 'A' && c <= 'Z')
            return enc(0x1D49C + static_cast<char32_t>(c - 'A'));
        if (c >= 'a' && c <= 'z')
            return enc(0x1D4B6 + static_cast<char32_t>(c - 'a'));
    } else if (styleName == "mathbf" || styleName == "boldsymbol" || styleName == "textbf") {
        if (c >= 'A' && c <= 'Z')
            return enc(0x1D400 + static_cast<char32_t>(c - 'A'));
        if (c >= 'a' && c <= 'z')
            return enc(0x1D41A + static_cast<char32_t>(c - 'a'));
        if (c >= '0' && c <= '9')
            return enc(0x1D7CE + static_cast<char32_t>(c - '0'));
    } else if (styleName == "mathfrak") {
        if (c >= 'A' && c <= 'Z')
            return enc(0x1D504 + static_cast<char32_t>(c - 'A'));
        if (c >= 'a' && c <= 'z')
            return enc(0x1D51E + static_cast<char32_t>(c - 'a'));
    }
    return std::string(1, c);
}

} // namespace qlab::ui::math
