// Spec 19 §3 "Run Controls" — device and backend selectors, shots, seed, noise, pulse level,
// snapshot cadence, Run / Stop / Step gate / Step shot, and the Compile Metrics sub-table
// (gate counts by type, depth, two-qubit count, SWAPs inserted, scheduled duration and the
// estimated hardware time of spec 15).
#include "Hardware/Hardware.hpp"
#include "UI/Format.hpp"
#include "UI/Panels/Panels.hpp"
#include "UI/Widgets/NumberField.hpp"
#include "UI/Widgets/Widgets.hpp"
#include <array>
#include <imgui.h>

namespace qlab::ui {
namespace {

constexpr std::array<std::string_view, 5> kBackendNames{"auto", "state vector", "density matrix",
                                                        "stabilizer", "Lindblad"};
constexpr std::array<std::string_view, 3> kNoiseNames{"ideal", "calibrated", "custom"};
constexpr std::array<std::string_view, 5> kCadenceNames{"none", "gate", "layer", "barrier", "end"};

class RunControlsPanel final : public BasicPanel {
  public:
    RunControlsPanel()
        : BasicPanel(PanelId::RunControls, "run_controls", "panels.run_controls", "▶",
                     Workspace::Program) {}

    void draw(UiContext& ctx) override;
    core::Json serialize() const override {
        core::Json j = core::Json::object();
        j["shots"] = shots_;
        j["seed"] = seed_;
        j["use_seed"] = useSeed_;
        j["backend"] = backend_;
        j["noise"] = noise_;
        j["cadence"] = cadence_;
        j["pulse_level"] = pulseLevel_;
        return j;
    }
    void deserialize(const core::Json& j) override;

  private:
    void drawMetrics(UiContext& ctx);

    std::int64_t shots_ = 1024, seed_ = 0;
    bool useSeed_ = false, pulseLevel_ = false;
    int backend_ = 0, noise_ = 1, cadence_ = 2;
};

void RunControlsPanel::deserialize(const core::Json& j) {
    if (!j.is_object())
        return;
    const auto num = [&](std::string_view key, auto& into) {
        if (const auto it = j.find(key); it != j.end() && it->is_number())
            into = it->template get<std::remove_reference_t<decltype(into)>>();
    };
    const auto flag = [&](std::string_view key, bool& into) {
        if (const auto it = j.find(key); it != j.end() && it->is_boolean())
            into = it->get<bool>();
    };
    num("shots", shots_);
    num("seed", seed_);
    num("backend", backend_);
    num("noise", noise_);
    num("cadence", cadence_);
    flag("use_seed", useSeed_);
    flag("pulse_level", pulseLevel_);
}

void RunControlsPanel::drawMetrics(UiContext& ctx) {
    widgets::sectionHeader(ctx, "Compile metrics");
    if (ctx.metrics == nullptr) {
        widgets::text(ctx, Token::TextSecondary, "Compile the program to see its metrics.");
        return;
    }
    const compiler::PassMetrics& m = *ctx.metrics;
    if (!ImGui::BeginTable("##metrics", 2, widgets::tableFlags(false), ImVec2(0.0f, 0.0f)))
        return;
    const auto row = [&](std::string_view label, std::string value) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        widgets::text(ctx, Token::TextSecondary, label);
        ImGui::TableNextColumn();
        FontScope f(*ctx.fonts, FontRole::Readout);
        widgets::text(ctx, Token::TextPrimary, value);
    };
    row("Gates", format::integer(m.gateCount));
    row("Two-qubit gates", format::integer(m.twoQubitCount));
    row("T count", format::integer(m.tCount));
    row("SWAPs inserted", format::integer(m.swapCount));
    row("Depth", format::integer(m.depth));
    // Picoseconds → seconds for the unit formatter (spec 10 integer time base).
    row("Scheduled duration",
        format::value(static_cast<double>(m.estimatedDuration.get()) * 1e-12, "s"));
    if (ctx.estimate != nullptr)
        row("Hardware time (est.)", format::duration(ctx.estimate->wallTime.valueS));
    ImGui::EndTable();
    if (ctx.estimate != nullptr) {
        ImGui::SameLine();
        widgets::fidelityBadge(ctx, ctx.estimate->cls);
    }
}

void RunControlsPanel::draw(UiContext& ctx) {
    const Metrics m = ctx.metrics_px();
    const bool running = ctx.session_view.status == SessionView::Status::Running;

    // ---- device and backend
    {
        const std::vector<std::string> ids = hw::shippedDeviceIds();
        std::vector<std::string_view> names;
        names.reserve(ids.size());
        for (const std::string& id : ids)
            names.emplace_back(id);
        int current = 0;
        for (std::size_t i = 0; i < ids.size(); ++i)
            if (ids[i] == ctx.session_view.device)
                current = static_cast<int>(i);
        if (!names.empty() &&
            widgets::combo(ctx, ctx.text("run.device"), &current, names, "Device") &&
            ctx.cmd.selectDevice)
            ctx.cmd.selectDevice(ids[static_cast<std::size_t>(current)]);
    }
    if (widgets::combo(ctx, ctx.text("run.backend"), &backend_, kBackendNames, "Backend") &&
        ctx.cmd.selectBackend)
        ctx.cmd.selectBackend(static_cast<runtime::BackendChoice>(backend_));
    if (!ctx.session_view.backendReason.empty())
        widgets::text(ctx, Token::TextSecondary, ctx.session_view.backendReason);

    // ---- shots, seed, noise, cadence
    widgets::intField(
        ctx, ctx.text("run.shots"), &shots_,
        widgets::FieldSpec{
            .step = 16.0, .lo = 1.0, .hi = static_cast<double>(runtime::RunOptions::kMaxShots)});
    widgets::checkbox(ctx, "Fixed seed", &useSeed_, "Seed");
    if (useSeed_) {
        ImGui::SameLine();
        widgets::intField(ctx, ctx.text("run.seed"), &seed_,
                          widgets::FieldSpec{.step = 1.0, .lo = 0.0});
    }
    widgets::combo(ctx, "Noise", &noise_, kNoiseNames, "Noise source");
    widgets::combo(ctx, ctx.text("run.snapshot_cadence"), &cadence_, kCadenceNames,
                   "Snapshot cadence");
    {
        // Spec 19 §2: pulse-level probing is Simulator-only tooling; it greys out in Physical lab.
        widgets::DisabledScope guard(ctx, ctx.physicalLab, ctx.text("app.physical_lab_tooltip"));
        widgets::checkbox(ctx, "Pulse level", &pulseLevel_, "Pulse level");
    }
    ImGui::Separator();

    // ---- transport (spec 19 §5 shortcuts F5 / Shift+F5 / F10 / F11)
    {
        widgets::DisabledScope guard(ctx, running || !ctx.cmd.run);
        if (widgets::primaryButton(ctx, std::string(ctx.text("menu.run_program")) + "  F5") &&
            ctx.cmd.run)
            ctx.cmd.run();
    }
    ImGui::SameLine();
    {
        widgets::DisabledScope guard(ctx, !running || !ctx.cmd.stop);
        if (widgets::dangerButton(ctx, std::string(ctx.text("menu.stop")) + "  Shift+F5") &&
            ctx.cmd.stop)
            ctx.cmd.stop();
    }
    ImGui::SameLine();
    if (widgets::secondaryButton(ctx, std::string(ctx.text("menu.compile")) + "  F6") &&
        ctx.cmd.compile)
        ctx.cmd.compile();
    ImGui::SameLine(0.0f, m.spacing(3));
    if (widgets::secondaryButton(ctx, "Step gate  F10") && ctx.cmd.stepGate)
        ctx.cmd.stepGate();
    ImGui::SameLine();
    if (widgets::secondaryButton(ctx, "Step shot  F11") && ctx.cmd.stepShot)
        ctx.cmd.stepShot();

    if (running && ctx.session_view.shotsTotal > 0)
        widgets::progressBar(ctx,
                             static_cast<double>(ctx.session_view.shotsDone) /
                                 static_cast<double>(ctx.session_view.shotsTotal),
                             format::integer(ctx.session_view.shotsDone) + " / " +
                                 format::integer(ctx.session_view.shotsTotal));
    drawMetrics(ctx);
}

} // namespace

PanelPtr makeRunControlsPanel() {
    return std::make_unique<RunControlsPanel>();
}

} // namespace qlab::ui
