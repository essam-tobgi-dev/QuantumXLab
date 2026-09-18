// Spec 21 §3.11 — Pauli expectation table (see PauliView.hpp).
#include "Viz/Views/PauliView.hpp"
#include "Data/Fidelity.hpp"
#include "Viz/Detail/ImGuiSupport.hpp"
#include "Viz/Math/Format.hpp"
#include "Viz/Math/Statistics.hpp"
#include "Viz/Widgets.hpp"
#include <algorithm>
#include <cmath>

namespace qlab::viz {
using namespace detail;

ReductionRequest PauliView::wants(const ViewInput& in) const {
    ReductionRequest r;
    if (in.qubitCount() > kInlineQubits) { // the O(2^n) sums belong to the run (spec 21 §2.3)
        r.singleQubitPaulis = true;
        r.pauliStrings = user_;
    }
    return r;
}

data::FidelityClass PauliView::fidelity(const ViewInput& in) const {
    if (in.snapshot) return in.stateClass();
    return in.counts ? data::FidelityClass::Statistical : data::FidelityClass::Exact;
}

Status PauliView::addPauli(std::string_view text) {
    const std::uint32_t n = nQubits_ > 0 ? nQubits_ : input().qubitCount();
    auto parsed = math::parseUserPauli(text, n);
    if (!parsed) return std::unexpected(parsed.error());
    const std::string label = parsed->label();
    if (std::find(user_.begin(), user_.end(), label) == user_.end()) user_.push_back(label);
    markDirty();
    return {};
}

void PauliView::removePauli(std::string_view text) {
    const auto it = std::find(user_.begin(), user_.end(), text);
    if (it == user_.end()) return;
    user_.erase(it);
    markDirty();
}

const PauliView::Row* PauliView::row(std::string_view label) const {
    for (const Row& r : rows_)
        if (r.data.label == label) return &r;
    return nullptr;
}

void PauliView::push(Row& row, std::optional<double> value) {
    if (!value) return;
    row.history.push_back(*value);
    while (row.history.size() > kHistory) row.history.pop_front();
}

void PauliView::rebuild(const ViewInput& in) {
    note_.clear();
    nQubits_ = in.qubitCount();
    if (nQubits_ == 0) {
        rows_.clear();
        note_ = "Run a program to see the Pauli expectations";
        return;
    }
    // Keep the histories across rebuilds: the rows are addressed by label.
    std::vector<Row> next;
    std::vector<qsim::PauliString> strings = math::singleQubitPaulis(nQubits_);
    for (const std::string& s : user_)
        if (auto p = math::parseUserPauli(s, nQubits_)) strings.push_back(std::move(*p));

    const bool inlineOk = nQubits_ <= kInlineQubits;
    for (std::size_t k = 0; k < strings.size(); ++k) {
        Row r;
        r.data.pauli = strings[k];
        r.data.label = math::pauliRowLabel(strings[k]);
        r.data.userAdded = k >= 3u * nQubits_;
        r.data.cls = fidelity(in);
        if (const Row* old = row(r.data.label)) r.history = old->history;

        if (in.reductions)
            if (const PauliValue* v = in.reductions->pauli(strings[k].label())) r.data.exact = v->value;
        if (!r.data.exact && in.snapshot && inlineOk) {
            if (in.snapshot->amplitudes) {
                if (auto e = math::pauliExpectation(*in.snapshot->amplitudes, strings[k])) r.data.exact = *e;
            } else if (in.snapshot->densityMatrix && in.snapshot->levels == 2) {
                if (auto e = math::pauliExpectation(*in.snapshot->densityMatrix, strings[k])) r.data.exact = *e;
            }
        }
        if (in.counts && in.measurement) {
            r.data.estimate = math::pauliFromCounts(*in.counts, strings[k], *in.measurement);
            r.measured = r.data.estimate.has_value();
        }
        push(r, r.data.exact ? r.data.exact : (r.data.estimate ? std::optional<double>{r.data.estimate->value} : std::nullopt));
        next.push_back(std::move(r));
    }
    rows_ = std::move(next);
    if (!inlineOk && !in.reductions) {
        note_ = "Waiting for the Pauli expectations from the run";
        setStale(true);
    }
}

void PauliView::layout() {
    rowHeight_ = ImGui::GetTextLineHeightWithSpacing() + 4.0f;
    headerHeight_ = rowHeight_;
}

std::optional<HitResult> PauliView::hitTest(glm::vec2 local) const {
    if (rows_.empty() || rowHeight_ <= 0.0f) return std::nullopt;
    if (local.y < headerHeight_ || local.x < 0.0f || local.x > bodySize().x) return std::nullopt;
    const auto k = static_cast<std::size_t>((local.y - headerHeight_) / rowHeight_);
    if (k >= rows_.size()) return std::nullopt;
    const Row& r = rows_[k];
    HitResult h;
    h.kind = HitKind::Row;
    h.row = static_cast<std::uint32_t>(k);
    h.title = "<" + r.data.label + ">";
    if (r.data.exact) h.readout.push_back({"exact", math::formatSig(*r.data.exact), input().stateClass()});
    if (r.data.estimate)
        h.readout.push_back({"shots", math::formatSig(r.data.estimate->value) + " ± " +
                                          math::formatSig(r.data.estimate->standardError, 2) + "  (N = " +
                                          std::to_string(r.data.estimate->shots) + ")",
                             data::FidelityClass::Statistical});
    else if (input().counts)
        h.readout.push_back({"shots", "not measured in this basis", data::FidelityClass::Statistical});
    // A single-qubit row names its qubit, so a click selects it everywhere (spec 21 §1.1).
    std::size_t support = 0, only = 0;
    for (std::size_t q = 0; q < r.data.pauli.size(); ++q)
        if (r.data.pauli.op(q) != 'I') {
            ++support;
            only = q;
        }
    if (support == 1) {
        h.kind = HitKind::Qubit;
        h.qubit = QubitIndex{static_cast<std::uint32_t>(only)};
    }
    return h;
}

std::optional<std::string> PauliView::exportCsv() const {
    if (rows_.empty()) return std::nullopt;
    std::string csv = "pauli,exact,shot_estimate,standard_error,shots\n";
    for (const Row& r : rows_) {
        csv += r.data.label + "," + (r.data.exact ? math::formatSig(*r.data.exact, 12) : std::string()) + ",";
        if (r.data.estimate)
            csv += math::formatSig(r.data.estimate->value, 12) + "," + math::formatSig(r.data.estimate->standardError, 12) +
                   "," + std::to_string(r.data.estimate->shots);
        else
            csv += ",,";
        csv += "\n";
    }
    return csv;
}

void PauliView::drawBody(DrawContext& ctx) {
    if (rows_.empty()) return widgets::placeholder(ctx, note_.empty() ? "No Pauli rows" : note_);
    const VizTheme& theme = *ctx.theme;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float sparkW = std::min(140.0f, 0.3f * bodySize().x);
    const float colLabel = 74.0f, colValue = 96.0f, colErr = 110.0f;

    drawText(dl, ImVec2(origin.x, origin.y + 2.0f), theme.textSecondary, "Pauli");
    drawText(dl, ImVec2(origin.x + colLabel, origin.y + 2.0f), theme.textSecondary, "<P>");
    drawText(dl, ImVec2(origin.x + colLabel + colValue, origin.y + 2.0f), theme.textSecondary, "shots");
    drawText(dl, ImVec2(origin.x + colLabel + colValue + colErr, origin.y + 2.0f), theme.textSecondary, "history");

    for (std::size_t k = 0; k < rows_.size(); ++k) {
        const Row& r = rows_[k];
        const float y = origin.y + headerHeight_ + static_cast<float>(k) * rowHeight_;
        if (y > origin.y + bodySize().y) break;
        if (k % 2 == 1)
            dl->AddRectFilled(ImVec2(origin.x, y), ImVec2(origin.x + bodySize().x, y + rowHeight_),
                              toU32(glm::vec3(theme.bgRaised), 0.5f));
        drawText(dl, ImVec2(origin.x, y + 2.0f), r.data.userAdded ? theme.accent : theme.textPrimary, r.data.label);
        drawText(dl, ImVec2(origin.x + colLabel, y + 2.0f), theme.textPrimary,
                 r.data.exact ? math::formatSig(*r.data.exact) : "—");
        drawText(dl, ImVec2(origin.x + colLabel + colValue, y + 2.0f),
                 r.measured ? theme.textPrimary : theme.textDisabled,
                 r.data.estimate ? math::formatSig(r.data.estimate->value) + " ± " +
                                       math::formatSig(r.data.estimate->standardError, 2)
                                 : (input().counts ? "not measured" : "—"));
        // Sparkline over the snapshot history, ⟨P⟩ ∈ [−1, 1] with the zero line drawn.
        if (r.history.size() > 1 && sparkW > 20.0f) {
            const float x0 = origin.x + colLabel + colValue + colErr;
            const float mid = y + 0.5f * rowHeight_, half = 0.5f * rowHeight_ - 3.0f;
            dl->AddLine(ImVec2(x0, mid), ImVec2(x0 + sparkW, mid), toU32(glm::vec3(theme.border), 0.6f));
            const float step = sparkW / static_cast<float>(r.history.size() - 1);
            for (std::size_t i = 1; i < r.history.size(); ++i)
                dl->AddLine(ImVec2(x0 + step * static_cast<float>(i - 1), mid - half * static_cast<float>(r.history[i - 1])),
                            ImVec2(x0 + step * static_cast<float>(i), mid - half * static_cast<float>(r.history[i])),
                            toU32(theme.accent), 1.4f);
        }
    }
    ImGui::Dummy(ImVec2(bodySize().x, headerHeight_ + static_cast<float>(rows_.size()) * rowHeight_));
}

} // namespace qlab::viz
