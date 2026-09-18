// Spec 19 §3 — the code editor widget (see CodeEditor.hpp).
#include "UI/Editor/CodeEditor.hpp"
#include "UI/Widgets/Widgets.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::ui::editor {
namespace {

using widgets::iv;
using widgets::u32;

constexpr int kGutterDigits = 5;

} // namespace

Token colorTokenFor(lang::TokenKind kind) {
    switch (kind) {
    case lang::TokenKind::Keyword: return Token::Accent;
    case lang::TokenKind::Type: return Token::ClassNumerical;
    case lang::TokenKind::Gate: return Token::Ok;
    case lang::TokenKind::Builtin: return Token::Warn;
    case lang::TokenKind::PhysicalQubit: return Token::SimOnly;
    case lang::TokenKind::Number:
    case lang::TokenKind::Duration: return Token::ClassModel;
    case lang::TokenKind::String:
    case lang::TokenKind::BitString: return Token::ClassStatistical;
    case lang::TokenKind::Comment: return Token::TextDisabled;
    case lang::TokenKind::Pragma: return Token::SimOnly;
    case lang::TokenKind::CalBlock: return Token::ClassIllustrative;
    case lang::TokenKind::Operator:
    case lang::TokenKind::Punct: return Token::TextSecondary;
    case lang::TokenKind::Invalid: return Token::Err;
    case lang::TokenKind::Identifier:
    case lang::TokenKind::Eof: break;
    }
    return Token::TextPrimary;
}

void CodeEditor::setCaret(Position p, bool select) {
    caret_ = model_.doc().clamp(p);
    if (!select) selection_ = caret_;
    revealLine_ = caret_.line;
}

void CodeEditor::revealLine(std::uint32_t line) { revealLine_ = line; }

void CodeEditor::gotoSpan(const SourceSpan& span) {
    setCaret(Position{std::max<std::uint32_t>(1, span.line), std::max<std::uint32_t>(1, span.column)});
}

std::string CodeEditor::selectedText() const {
    return hasSelection() ? model_.doc().textBetween(std::min(selection_, caret_), std::max(selection_, caret_))
                          : std::string{};
}

void CodeEditor::deleteSelection() {
    if (!hasSelection()) return;
    const Position from = std::min(selection_, caret_), to = std::max(selection_, caret_);
    model_.doc().erase(from, to);
    caret_ = selection_ = from;
}

void CodeEditor::insertText(std::string_view what) {
    deleteSelection();
    model_.doc().insert(caret_, what);
    caret_ = model_.doc().positionOf(model_.doc().offsetOf(caret_) + what.size());
    selection_ = caret_;
}

void CodeEditor::undo() {
    Position p = caret_;
    if (model_.doc().undo(&p)) setCaret(p);
}
void CodeEditor::redo() {
    Position p = caret_;
    if (model_.doc().redo(&p)) setCaret(p);
}

bool CodeEditor::gotoDefinition() {
    const std::string word = model_.wordAt(caret_);
    if (word.empty()) return false;
    const auto def = model_.definitionOf(word);
    if (!def) return false;
    setCaret(def->at);
    return true;
}

bool CodeEditor::applyFixIt() {
    for (const Marker* m : model_.markersOn(caret_.line)) {
        if (m->fix.empty()) continue;
        const Position from{m->line, m->begin}, to{m->line, m->end};
        // A fix already in the buffer is not re-applied: it would be a no-op undo entry.
        if (model_.doc().textBetween(from, to) == m->fix) continue;
        model_.doc().replace(from, to, m->fix);
        setCaret(Position{m->line, m->begin + codePoints(m->fix)});
        return true;
    }
    return false;
}

core::Json CodeEditor::serialize() const {
    core::Json j = core::Json::object();
    j["text"] = model_.doc().text();
    j["caret_line"] = caret_.line;
    j["caret_column"] = caret_.column;
    return j;
}

void CodeEditor::deserialize(const core::Json& j) {
    if (!j.is_object()) return;
    if (const auto it = j.find("text"); it != j.end() && it->is_string()) model_.setSource(it->get<std::string>());
    Position p{1, 1};
    if (const auto it = j.find("caret_line"); it != j.end() && it->is_number_unsigned()) p.line = it->get<std::uint32_t>();
    if (const auto it = j.find("caret_column"); it != j.end() && it->is_number_unsigned())
        p.column = it->get<std::uint32_t>();
    setCaret(p);
}

Position CodeEditor::positionAt(ImVec2 local, float charWidth, float lineHeight) const {
    const auto line = static_cast<std::uint32_t>(std::max(0.0f, local.y) / std::max(1.0f, lineHeight)) + 1;
    const auto column = static_cast<std::uint32_t>(std::max(0.0f, local.x) / std::max(1.0f, charWidth) + 0.5f) + 1;
    return model_.doc().clamp(Position{line, column});
}

void CodeEditor::drawGutter(UiContext& ctx, std::uint32_t line, ImVec2 at, float gutterWidth) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%*u", kGutterDigits, line);
    const bool current = line == caret_.line;
    dl->AddText(at, u32(ctx.th()[current ? Token::TextPrimary : Token::TextDisabled]), buf);
    // Spec 19 §3: the severity icon of the worst diagnostic on this line.
    const auto markers = model_.markersOn(line);
    if (markers.empty()) return;
    lang::Severity worst = lang::Severity::Info;
    for (const Marker* m : markers) worst = std::min(worst, m->severity); // Error < Warning < Info
    const Token token = worst == lang::Severity::Error     ? Token::Err
                        : worst == lang::Severity::Warning ? Token::Warn
                                                           : Token::Accent;
    dl->AddCircleFilled(ImVec2(at.x + gutterWidth - ctx.ui(6.0f), at.y + ImGui::GetTextLineHeight() * 0.5f),
                        ctx.ui(3.5f), u32(ctx.th()[token]));
}

void CodeEditor::drawLine(UiContext& ctx, std::uint32_t line, ImVec2 at, float charWidth) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const std::string_view text = model_.doc().line(line);
    const float height = ImGui::GetTextLineHeight();

    // Token runs; anything the tokenizer did not cover falls back to the primary tone.
    std::uint32_t covered = 1;
    const auto emit = [&](std::uint32_t begin, std::uint32_t end, Token token) {
        if (end <= begin) return;
        const std::size_t b = model_.doc().columnToByte(line, begin);
        const std::size_t e = model_.doc().columnToByte(line, end);
        if (e <= b) return;
        dl->AddText(ImVec2(at.x + static_cast<float>(begin - 1) * charWidth, at.y),
                    u32(Theme::readableText(ctx.th()[token], ctx.th()[Token::BgPanel])), text.data() + b,
                    text.data() + e);
    };
    for (const HighlightSpan& h : model_.highlightsOn(line)) {
        emit(covered, h.begin, Token::TextPrimary);
        emit(h.begin, h.end, colorTokenFor(h.kind));
        covered = std::max(covered, h.end);
    }
    emit(covered, model_.doc().lineLength(line) + 1, Token::TextPrimary);

    // Squiggles under the diagnostic spans, faded while the compile is still catching up with the
    // last keystroke (spec 14 §11: the incremental compiler debounces 150 ms).
    const float markerAlpha = model_.markersStale() ? 0.4f : 1.0f;
    for (const Marker* m : model_.markersOn(line)) {
        const Token base = m->severity == lang::Severity::Error     ? Token::Err
                           : m->severity == lang::Severity::Warning ? Token::Warn
                                                                    : Token::Accent;
        const Color token(glm::vec3(ctx.th()[base]), markerAlpha);
        const float x0 = at.x + static_cast<float>(m->begin - 1) * charWidth;
        const float x1 = at.x + static_cast<float>(std::max(m->end, m->begin + 1) - 1) * charWidth;
        const float y = at.y + height - 1.0f;
        for (float x = x0; x < x1; x += ctx.ui(4.0f))
            dl->AddLine(ImVec2(x, y), ImVec2(std::min(x + ctx.ui(2.0f), x1), y - ctx.ui(1.5f)),
                        u32(token), ctx.metrics_px().border);
    }
}

void CodeEditor::drawCompletionPopup(UiContext& ctx, ImVec2 caretScreen, float lineHeight) {
    if (!completionOpen_) return;
    completions_ = model_.completions(caret_);
    if (completions_.empty()) {
        completionOpen_ = false;
        return;
    }
    completionIndex_ = std::clamp(completionIndex_, 0, static_cast<int>(completions_.size()) - 1);
    ImGui::SetNextWindowPos(ImVec2(caretScreen.x, caretScreen.y + lineHeight));
    ImGui::SetNextWindowSize(ImVec2(ctx.ui(320.0f), 0.0f));
    if (ImGui::Begin("##completions", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_AlwaysAutoResize)) {
        for (int i = 0; i < static_cast<int>(completions_.size()) && i < 12; ++i) {
            const Completion& c = completions_[static_cast<std::size_t>(i)];
            const bool selected = i == completionIndex_;
            if (ImGui::Selectable(c.text.c_str(), selected)) {
                caret_ = model_.applyCompletion(caret_, c);
                selection_ = caret_;
                completionOpen_ = false;
            }
            ImGui::SameLine();
            widgets::text(ctx, Token::TextSecondary, completionKindName(c.kind));
            if (!c.detail.empty()) {
                ImGui::SameLine();
                widgets::text(ctx, Token::TextDisabled, c.detail);
            }
        }
    }
    ImGui::End();
}

bool CodeEditor::handleKeys(UiContext& ctx) {
    Document& doc = model_.doc();
    const ImGuiIO& io = ImGui::GetIO();
    const bool ctrl = io.KeyCtrl || io.KeySuper;
    const bool shift = io.KeyShift;
    bool changed = false;

    if (completionOpen_) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) ++completionIndex_;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) --completionIndex_;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) completionOpen_ = false;
        if ((ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_Tab, false)) &&
            !completions_.empty()) {
            const auto i = static_cast<std::size_t>(std::clamp(completionIndex_, 0, static_cast<int>(completions_.size()) - 1));
            caret_ = model_.applyCompletion(caret_, completions_[i]);
            selection_ = caret_;
            completionOpen_ = false;
            return true;
        }
    }

    for (ImWchar c : io.InputQueueCharacters) {
        if (c < 32 && c != '\t') continue;
        char utf8[5] = {};
        int n = 0;
        if (c < 0x80) utf8[n++] = static_cast<char>(c);
        else if (c < 0x800) {
            utf8[n++] = static_cast<char>(0xC0 | (c >> 6));
            utf8[n++] = static_cast<char>(0x80 | (c & 0x3F));
        } else {
            utf8[n++] = static_cast<char>(0xE0 | (c >> 12));
            utf8[n++] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            utf8[n++] = static_cast<char>(0x80 | (c & 0x3F));
        }
        insertText(std::string_view(utf8, static_cast<std::size_t>(n)));
        changed = true;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Enter, true)) {
        // Auto-indent (spec 19 §3).
        const std::string indent = model_.indentAfter(caret_.line);
        insertText("\n" + indent);
        changed = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true)) {
        if (hasSelection()) deleteSelection();
        else if (caret_ > Position{1, 1}) {
            const Position before = doc.positionOf(doc.offsetOf(caret_) - 1);
            doc.erase(before, caret_);
            caret_ = selection_ = before;
        }
        changed = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, true)) {
        if (hasSelection()) deleteSelection();
        else {
            const Position after = doc.positionOf(doc.offsetOf(caret_) + 1);
            doc.erase(caret_, after);
        }
        changed = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) setCaret(doc.positionOf(doc.offsetOf(caret_) - 1), shift);
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) setCaret(doc.positionOf(doc.offsetOf(caret_) + 1), shift);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true) && !completionOpen_)
        setCaret(Position{caret_.line > 1 ? caret_.line - 1 : 1, caret_.column}, shift);
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true) && !completionOpen_)
        setCaret(Position{caret_.line + 1, caret_.column}, shift);
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) setCaret(Position{caret_.line, 1}, shift);
    if (ImGui::IsKeyPressed(ImGuiKey_End, false)) setCaret(Position{caret_.line, doc.lineLength(caret_.line) + 1}, shift);

    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
        selection_ = Position{1, 1};
        caret_ = doc.end();
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && hasSelection())
        ImGui::SetClipboardText(selectedText().c_str());
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
        if (const char* clip = ImGui::GetClipboardText(); clip != nullptr) {
            insertText(clip);
            changed = true;
        }
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Space, false)) completionOpen_ = true;
    if (ImGui::IsKeyPressed(ImGuiKey_F12, false)) gotoDefinition();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Period, false)) changed |= applyFixIt();
    if (ctrl && !shift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        undo();
        changed = true;
    }
    if (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
        redo();
        changed = true;
    }
    (void)ctx;
    return changed;
}

bool CodeEditor::draw(UiContext& ctx) {
    const Document& doc = model_.doc();
    FontScope font(*ctx.fonts, FontRole::Code);
    const float lineHeight = ImGui::GetTextLineHeight();
    const float charWidth = ImGui::CalcTextSize("0").x;
    const float gutterWidth = charWidth * (kGutterDigits + 2);

    ImGui::BeginChild("##editor", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNav);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    focused_ = ImGui::IsWindowFocused();
    bool changed = false;

    const auto lines = static_cast<int>(doc.lineCount());
    const float width = std::max(ImGui::GetContentRegionAvail().x, gutterWidth + charWidth * 120.0f);
    ImGui::InvisibleButton("##surface", ImVec2(width, lineHeight * static_cast<float>(lines) + lineHeight));
    if (ImGui::IsItemClicked()) {
        ImGui::SetKeyboardFocusHere(-1);
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        setCaret(positionAt(ImVec2(mouse.x - origin.x - gutterWidth, mouse.y - origin.y), charWidth, lineHeight),
                 ImGui::GetIO().KeyShift);
        focused_ = true;
    }
    if (focused_) changed = handleKeys(ctx);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Only the visible rows are painted: the cost is independent of the file length.
    const float scroll = ImGui::GetScrollY();
    // The visible height, not what is left of the content region: `InvisibleButton("##surface")`
    // above has already consumed it, so `GetContentRegionAvail()` here measures the remainder and
    // the editor would paint only the first few rows of a file that fills the panel.
    const float viewHeight = scroll + ImGui::GetWindowHeight();
    const auto first = std::max(1, static_cast<int>(scroll / lineHeight));
    const auto last = std::min(lines, static_cast<int>(viewHeight / lineHeight) + 2);

    // Current-line highlight and the matching bracket (spec 19 §3).
    const auto match = model_.matchingBracket(caret_);
    for (int l = first; l <= last; ++l) {
        const auto line = static_cast<std::uint32_t>(l);
        const ImVec2 at(origin.x, origin.y + static_cast<float>(l - 1) * lineHeight);
        if (line == caret_.line)
            dl->AddRectFilled(at, ImVec2(at.x + width, at.y + lineHeight),
                              u32(Color(glm::vec3(ctx.th()[Token::Accent]), 0.08f)));
        drawGutter(ctx, line, at, gutterWidth);
        drawLine(ctx, line, ImVec2(at.x + gutterWidth, at.y), charWidth);
        if (match && match->line == line) {
            const float x = at.x + gutterWidth + static_cast<float>(match->column - 1) * charWidth;
            dl->AddRect(ImVec2(x, at.y), ImVec2(x + charWidth, at.y + lineHeight), u32(ctx.th()[Token::Accent]),
                        ctx.metrics_px().radiusSm);
        }
    }
    // Selection band and caret.
    if (hasSelection()) {
        const Position a = std::min(selection_, caret_), b = std::max(selection_, caret_);
        for (std::uint32_t l = a.line; l <= b.line && l <= doc.lineCount(); ++l) {
            const std::uint32_t c0 = l == a.line ? a.column : 1;
            const std::uint32_t c1 = l == b.line ? b.column : doc.lineLength(l) + 1;
            const float y = origin.y + static_cast<float>(l - 1) * lineHeight;
            dl->AddRectFilled(ImVec2(origin.x + gutterWidth + static_cast<float>(c0 - 1) * charWidth, y),
                              ImVec2(origin.x + gutterWidth + static_cast<float>(c1 - 1) * charWidth, y + lineHeight),
                              u32(ctx.th()[Token::AccentSoft]));
        }
    }
    const ImVec2 caretScreen(origin.x + gutterWidth + static_cast<float>(caret_.column - 1) * charWidth,
                             origin.y + static_cast<float>(caret_.line - 1) * lineHeight);
    if (focused_ && (ctx.reducedMotion || std::fmod(ctx.timeS, 1.0) < 0.5))
        dl->AddLine(caretScreen, ImVec2(caretScreen.x, caretScreen.y + lineHeight), u32(ctx.th()[Token::TextPrimary]),
                    ctx.metrics_px().border);

    if (revealLine_ != 0) {
        ImGui::SetScrollY(std::max(0.0f, static_cast<float>(revealLine_ - 3) * lineHeight));
        revealLine_ = 0;
    }
    // Hover documentation for gates and builtins (spec 19 §3).
    if (ImGui::IsItemHovered() && ctx.assets != nullptr) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const Position p = positionAt(ImVec2(mouse.x - origin.x - gutterWidth, mouse.y - origin.y), charWidth, lineHeight);
        if (const GateDocEntry* doc_entry = model_.hoverDoc(p, *ctx.assets);
            doc_entry != nullptr && ImGui::BeginTooltip()) {
            ImGui::PushTextWrapPos(ctx.ui(420.0f));
            widgets::text(ctx, Token::TextPrimary, doc_entry->signature);
            widgets::textWrapped(ctx, Token::TextSecondary, doc_entry->description);
            if (!doc_entry->decomposition.empty())
                widgets::labelled(ctx, "Decomposition", doc_entry->decomposition);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        // A diagnostic under the cursor shows its message and fix-it hint.
        for (const Marker* m : model_.markersOn(p.line)) {
            if (p.column < m->begin || p.column > m->end) continue;
            if (!ImGui::BeginTooltip()) break;
            widgets::text(ctx, m->severity == lang::Severity::Error ? Token::Err : Token::Warn,
                          m->id + ": " + m->message);
            if (!m->fix.empty()) widgets::labelled(ctx, "Fix", m->fix);
            ImGui::EndTooltip();
            break;
        }
    }
    drawCompletionPopup(ctx, caretScreen, lineHeight);
    ImGui::EndChild();
    return changed;
}

} // namespace qlab::ui::editor
