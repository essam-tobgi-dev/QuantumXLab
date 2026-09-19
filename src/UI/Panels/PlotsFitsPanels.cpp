// Spec 19 §3 "Plots" and "Fits" — ImPlot time-series / XY / histogram views over `data::Series`
// (spec 22 §2) with dashed theory overlays and a cursor readout, and the fit-model chooser with its
// parameters ± 1σ, χ²_ν and residual plot (spec 22 §4, §5).
#include "Data/Models.hpp"
#include "UI/Format.hpp"
#include "UI/Panels/Panels.hpp"
#include "UI/Widgets/EquationView.hpp"
#include "UI/Widgets/NumberField.hpp"
#include "UI/Widgets/Widgets.hpp"
#include <algorithm>
#include <implot.h>

namespace qlab::ui {
namespace {

using widgets::iv;

// Spec 19 §1: ImPlot draws in theme tokens only, never its own defaults.
class PlotStyle {
  public:
    explicit PlotStyle(const UiContext& ctx) {
        ImPlot::PushStyleColor(ImPlotCol_FrameBg, iv(ctx.th()[Token::BgPanel]));
        ImPlot::PushStyleColor(ImPlotCol_PlotBg, iv(ctx.th()[Token::BgPanel]));
        ImPlot::PushStyleColor(ImPlotCol_PlotBorder, iv(ctx.th()[Token::Border]));
        ImPlot::PushStyleColor(ImPlotCol_AxisText, iv(ctx.th()[Token::TextSecondary]));
        ImPlot::PushStyleColor(ImPlotCol_AxisGrid,
                               iv(Color(glm::vec3(ctx.th()[Token::Border]), 0.5f)));
        ImPlot::PushStyleColor(ImPlotCol_LegendBg, iv(ctx.th()[Token::BgRaised]));
        ImPlot::PushStyleColor(ImPlotCol_LegendText, iv(ctx.th()[Token::TextPrimary]));
        ImPlot::PushStyleColor(ImPlotCol_InlayText, iv(ctx.th()[Token::TextSecondary]));
    }
    ~PlotStyle() { ImPlot::PopStyleColor(8); }
    PlotStyle(const PlotStyle&) = delete;
    PlotStyle& operator=(const PlotStyle&) = delete;
};

std::vector<data::ChannelDesc> channelsOf(const UiContext& ctx) {
    return ctx.recorder != nullptr ? ctx.recorder->channels() : std::vector<data::ChannelDesc>{};
}

class PlotsPanel final : public BasicPanel {
  public:
    PlotsPanel() : BasicPanel(PanelId::Plots, "plots", "panels.plots", "◇", Workspace::Analysis) {}

    void draw(UiContext& ctx) override;
    core::Json serialize() const override {
        core::Json j = core::Json::object();
        j["channel"] = channel_;
        j["show_theory"] = showTheory_;
        return j;
    }
    void deserialize(const core::Json& j) override {
        if (!j.is_object())
            return;
        if (const auto it = j.find("channel"); it != j.end() && it->is_number_integer())
            channel_ = it->get<int>();
        if (const auto it = j.find("show_theory"); it != j.end() && it->is_boolean())
            showTheory_ = it->get<bool>();
    }

  private:
    int channel_ = 0;
    bool showTheory_ = true;
};

void PlotsPanel::draw(UiContext& ctx) {
    const std::vector<data::ChannelDesc> descs = channelsOf(ctx);
    if (descs.empty()) {
        widgets::placeholder(ctx, "No recorded channels yet.");
        return;
    }
    std::vector<std::string_view> names;
    names.reserve(descs.size());
    for (const data::ChannelDesc& d : descs)
        names.emplace_back(d.id);
    channel_ = std::clamp(channel_, 0, static_cast<int>(descs.size()) - 1);
    widgets::combo(ctx, "Channel", &channel_, names, "Plot channel");
    ImGui::SameLine();
    widgets::checkbox(ctx, "Theory overlay", &showTheory_, "Theory overlay");
    const data::ChannelDesc& desc = descs[static_cast<std::size_t>(channel_)];
    ImGui::SameLine();
    widgets::fidelityBadge(ctx, desc.cls);

    const auto id = ctx.recorder->find(desc.id);
    if (!id)
        return;
    // Spec 22 §1: min/max envelope decimation keeps the draw cost bounded whatever the length.
    const data::Series series = ctx.recorder->decimated(*id, -1e300, 1e300, 4096);
    if (series.empty()) {
        widgets::text(ctx, Token::TextSecondary, "Channel is empty.");
        return;
    }
    PlotStyle style(ctx);
    if (ImPlot::BeginPlot("##plot", ImVec2(-1.0f, -1.0f))) {
        ImPlot::SetupAxes(desc.xUnit.c_str(), desc.unit.c_str());
        for (std::size_t k = 0; k < series.y.size(); ++k) {
            const Color c = ctx.th().qubitColor(static_cast<std::uint32_t>(k));
            ImPlot::SetNextLineStyle(iv(c));
            // Spec 19 §8: colour never carries meaning alone — every series has its own marker.
            ImPlot::SetNextMarkerStyle(static_cast<ImPlotMarker>(k % ImPlotMarker_COUNT), 3.0f,
                                       iv(c));
            const std::string label =
                desc.id + (series.y.size() > 1 ? "[" + std::to_string(k) + "]" : "");
            ImPlot::PlotLine(label.c_str(), series.x.data(), series.y[k].data(),
                             static_cast<int>(series.x.size()));
        }
        // Spec 22 §3: theory overlays are dashed in the secondary text colour.
        if (showTheory_ && !series.markers.empty()) {
            for (const data::Marker& mark : series.markers) {
                double x[2] = {mark.x, mark.x};
                double y[2] = {-1e300, 1e300};
                ImPlot::SetNextLineStyle(iv(ctx.th()[Token::TextSecondary]), 1.0f);
                ImPlot::PlotLine(mark.label.c_str(), x, y, 2);
            }
        }
        // Cursor readout with the measurement delta (spec 19 §3).
        if (ImPlot::IsPlotHovered()) {
            const ImPlotPoint at = ImPlot::GetPlotMousePos();
            ImPlot::Annotation(at.x, at.y, iv(ctx.th()[Token::Accent]), ImVec2(6.0f, -6.0f), true,
                               "%s, %s", format::value(at.x, desc.xUnit).c_str(),
                               format::value(at.y, desc.unit).c_str());
        }
        ImPlot::EndPlot();
    }
}

// ---------------------------------------------------------------- Fits (spec 22 §4, §5)

class FitsPanel final : public BasicPanel {
  public:
    FitsPanel() : BasicPanel(PanelId::Fits, "fits", "panels.fits", "△", Workspace::Analysis) {}

    void draw(UiContext& ctx) override;
    core::Json serialize() const override {
        core::Json j = core::Json::object();
        j["model"] = model_;
        j["channel"] = channel_;
        return j;
    }
    void deserialize(const core::Json& j) override {
        if (!j.is_object())
            return;
        if (const auto it = j.find("model"); it != j.end() && it->is_number_integer())
            model_ = it->get<int>();
        if (const auto it = j.find("channel"); it != j.end() && it->is_number_integer())
            channel_ = it->get<int>();
    }

  private:
    int model_ = 0, channel_ = 0;
    std::optional<data::fit::FitResult> result_;
    std::string error_;
    EquationView equation_;
};

void FitsPanel::draw(UiContext& ctx) {
    const std::vector<std::string> ids = data::fit::modelIds();
    std::vector<std::string_view> modelNames;
    modelNames.reserve(ids.size());
    for (const std::string& id : ids)
        modelNames.emplace_back(id);
    model_ = std::clamp(model_, 0, static_cast<int>(ids.size()) - 1);
    widgets::combo(ctx, "Model", &model_, modelNames, "Fit model");

    const std::vector<data::ChannelDesc> descs = channelsOf(ctx);
    if (descs.empty()) {
        widgets::placeholder(ctx, "No recorded channels to fit.");
        return;
    }
    std::vector<std::string_view> names;
    names.reserve(descs.size());
    for (const data::ChannelDesc& d : descs)
        names.emplace_back(d.id);
    channel_ = std::clamp(channel_, 0, static_cast<int>(descs.size()) - 1);
    widgets::combo(ctx, "Channel", &channel_, names, "Fit channel");

    const std::unique_ptr<data::fit::FitModel> model =
        data::fit::makeModel(ids[static_cast<std::size_t>(model_)]);
    if (model != nullptr) {
        equation_.setLatex(model->latex());
        equation_.setDisplayStyle(false);
        equation_.draw(ctx);
    }
    ImGui::SameLine();
    if (widgets::primaryButton(ctx, "Fit") && model != nullptr && ctx.recorder != nullptr) {
        error_.clear();
        result_.reset();
        if (const auto id = ctx.recorder->find(descs[static_cast<std::size_t>(channel_)].id)) {
            const data::Series s = ctx.recorder->view(*id);
            if (s.y.empty() || s.x.size() < 3)
                error_ = "the channel has too few samples to fit";
            else if (auto r = data::fit::fitModel(*model, s.x, s.y[0]))
                result_ = std::move(*r);
            else
                error_ = r.error().message;
        }
    }
    if (!error_.empty()) {
        widgets::text(ctx, Token::Err, error_);
        return;
    }
    if (!result_) {
        widgets::text(ctx, Token::TextSecondary, "Choose a model and press Fit.");
        return;
    }
    const data::fit::FitResult& r = *result_;
    widgets::labelled(ctx, "χ²/ν", format::number(r.chi2ndf));
    ImGui::SameLine();
    widgets::labelled(ctx, "R²", format::number(r.r2));
    ImGui::SameLine();
    widgets::badge(ctx, r.converged ? "converged" : "not converged",
                   ctx.th()[r.converged ? Token::Ok : Token::Warn]);
    ImGui::SameLine();
    widgets::fidelityBadge(ctx, r.cls);

    if (ImGui::BeginTable("##params", 3, widgets::tableFlags(false))) {
        ImGui::TableSetupColumn("parameter", ImGuiTableColumnFlags_WidthStretch, 0.34f);
        ImGui::TableSetupColumn("value ± 1σ", ImGuiTableColumnFlags_WidthStretch, 0.46f);
        ImGui::TableSetupColumn("at bound", ImGuiTableColumnFlags_WidthStretch, 0.20f);
        ImGui::TableHeadersRow();
        const std::vector<data::fit::ParamInfo> info =
            model != nullptr ? model->params() : std::vector<data::fit::ParamInfo>{};
        for (std::size_t i = 0; i < r.beta.size(); ++i) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            widgets::text(ctx, Token::TextSecondary,
                          i < r.paramNames.size() ? r.paramNames[i] : std::to_string(i));
            ImGui::TableNextColumn();
            FontScope f(*ctx.fonts, FontRole::Readout);
            const std::string unit = i < info.size() ? info[i].unit : std::string{};
            widgets::text(
                ctx, Token::TextPrimary,
                format::withSigma(r.beta[i], i < r.sigma.size() ? r.sigma[i] : 0.0, unit));
            ImGui::TableNextColumn();
            if (i < r.atBound.size() && r.atBound[i])
                widgets::badge(ctx, "bound", ctx.th()[Token::Warn]);
        }
        ImGui::EndTable();
    }
    for (const auto& [name, value] : r.derived)
        widgets::labelled(ctx, name, format::number(value));

    if (r.residuals.empty())
        return;
    PlotStyle style(ctx);
    if (ImPlot::BeginPlot("##residuals", ImVec2(-1.0f, ctx.ui(140.0f)))) {
        ImPlot::SetupAxes("sample", "residual");
        std::vector<double> index(r.residuals.size());
        for (std::size_t i = 0; i < index.size(); ++i)
            index[i] = static_cast<double>(i);
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 2.5f, iv(ctx.th()[Token::Accent]));
        ImPlot::PlotScatter("residual", index.data(), r.residuals.data(),
                            static_cast<int>(index.size()));
        ImPlot::EndPlot();
    }
}

} // namespace

PanelPtr makePlotsPanel() {
    return std::make_unique<PlotsPanel>();
}
PanelPtr makeFitsPanel() {
    return std::make_unique<FitsPanel>();
}

} // namespace qlab::ui
