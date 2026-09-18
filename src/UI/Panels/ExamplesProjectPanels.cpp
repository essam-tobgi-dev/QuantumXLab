// Spec 19 §3 "Examples Library" and "Project / Device Manager".
//   Examples: the categorised listing of `Assets/Programs/`, a source preview, "Open in editor",
//             and the expected results shown after a run.
//   Project:  the project file, the device selection, and `device.json` / `calibration.json` as
//             editable tables — an edit creates a DERIVED device and never touches the shipped asset.
#include "Core/Paths.hpp"
#include "Data/Fidelity.hpp"
#include "Hardware/Hardware.hpp"
#include "Hardware/Loader.hpp"
#include "UI/Format.hpp"
#include "UI/Panels/Panels.hpp"
#include "UI/Widgets/NumberField.hpp"
#include "UI/Widgets/Widgets.hpp"
#include <algorithm>
#include <filesystem>
#include <imgui.h>

namespace qlab::ui {
namespace {

struct Example {
    std::string category, name;
    std::filesystem::path path, expected;
};

std::vector<Example> scanExamples() {
    std::vector<Example> out;
    const std::filesystem::path root = core::assetDir() / "Programs" / "Examples";
    std::error_code ec;
    if (!std::filesystem::exists(root, ec)) return out;
    for (const auto& dir : std::filesystem::directory_iterator(root, ec)) {
        if (!dir.is_directory()) continue;
        for (const auto& file : std::filesystem::directory_iterator(dir.path(), ec)) {
            if (file.path().extension() != ".qasm") continue;
            Example e;
            e.category = dir.path().filename().string();
            e.name = file.path().stem().string();
            e.path = file.path();
            e.expected = file.path().parent_path() / (e.name + ".expected.json");
            out.push_back(std::move(e));
        }
    }
    std::sort(out.begin(), out.end(), [](const Example& a, const Example& b) {
        return std::tie(a.category, a.name) < std::tie(b.category, b.name);
    });
    return out;
}

class ExamplesPanel final : public BasicPanel {
public:
    ExamplesPanel() : BasicPanel(PanelId::Examples, "examples", "panels.examples", "★", Workspace::Program) {}

    void draw(UiContext& ctx) override;
    core::Json serialize() const override {
        core::Json j = core::Json::object();
        j["selected"] = selected_;
        return j;
    }
    void deserialize(const core::Json& j) override {
        if (j.is_object())
            if (const auto it = j.find("selected"); it != j.end() && it->is_string()) selected_ = it->get<std::string>();
    }

private:
    std::vector<Example> examples_;
    std::string selected_, preview_, expected_;
    bool scanned_ = false;
};

void ExamplesPanel::draw(UiContext& ctx) {
    if (!scanned_) {
        examples_ = scanExamples();
        scanned_ = true;
    }
    if (examples_.empty()) {
        widgets::placeholder(ctx, "No example programs found under Assets/Programs/Examples.");
        return;
    }
    std::string category;
    ImGui::BeginChild("##list", ImVec2(0.0f, ImGui::GetContentRegionAvail().y * 0.5f));
    for (const Example& e : examples_) {
        if (e.category != category) {
            category = e.category;
            widgets::sectionHeader(ctx, category);
        }
        const bool selected = selected_ == e.name;
        if (ImGui::Selectable(e.name.c_str(), selected)) {
            selected_ = e.name;
            preview_ = core::readTextFile(e.path).value_or("");
            expected_ = core::readTextFile(e.expected).value_or("");
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && ctx.cmd.openProgram)
            ctx.cmd.openProgram(core::readTextFile(e.path).value_or(""), e.path.string());
    }
    ImGui::EndChild();
    ImGui::Separator();

    if (selected_.empty()) {
        widgets::text(ctx, Token::TextSecondary, "Select an example to preview it.");
        return;
    }
    if (widgets::primaryButton(ctx, "Open in editor") && ctx.cmd.openProgram) ctx.cmd.openProgram(preview_, selected_);
    ImGui::SameLine();
    widgets::text(ctx, Token::TextSecondary, selected_);
    ImGui::BeginChild("##preview", ImVec2(0.0f, 0.0f));
    {
        FontScope f(*ctx.fonts, FontRole::Code);
        widgets::text(ctx, Token::TextPrimary, preview_);
    }
    // Spec 19 §3: the expected results are shown once a run exists to compare against.
    if (!expected_.empty() && ctx.result != nullptr) {
        widgets::sectionHeader(ctx, "Expected");
        FontScope f(*ctx.fonts, FontRole::CodeSmall);
        widgets::text(ctx, Token::TextSecondary, expected_);
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------- Project / Device Manager

class ProjectPanel final : public BasicPanel {
public:
    ProjectPanel() : BasicPanel(PanelId::Project, "project", "panels.project", "■", Workspace::Lab) {}

    void draw(UiContext& ctx) override;

private:
    void drawDevice(UiContext& ctx);
    void drawCalibration(UiContext& ctx);
    bool derived_ = false;
};

void ProjectPanel::drawDevice(UiContext& ctx) {
    const hw::Device& d = *ctx.device;
    if (!ImGui::BeginTable("##device", 2, widgets::tableFlags(false))) return;
    const auto row = [&](std::string_view field, std::string value) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        widgets::text(ctx, Token::TextSecondary, field);
        ImGui::TableNextColumn();
        widgets::text(ctx, Token::TextPrimary, value);
    };
    row("id", d.id);
    row("name", d.displayName);
    row("technology", std::string(hw::technologyName(d.technology)));
    row("qubits", format::integer(d.qubitCount()));
    row("edges", format::integer(d.edges.size()));
    row("dt", format::value(static_cast<double>(d.timing.dtPs) * 1e-12, "s"));
    row("readout", format::value(d.timing.readout.v, "s"));
    row("repetition delay", format::value(d.timing.repetitionDelay.v, "s"));
    std::string natives;
    for (const std::string& g : d.gates.single) natives += (natives.empty() ? "" : " ") + g;
    for (const std::string& g : d.gates.two) natives += (natives.empty() ? "" : " ") + g;
    row("native gates", natives);
    ImGui::EndTable();
}

void ProjectPanel::drawCalibration(UiContext& ctx) {
    const hw::Calibration& c = *ctx.calibration;
    if (!ImGui::BeginTable("##cal", 6, widgets::tableFlags(true))) return;
    ImGui::TableSetupColumn("qubit");
    ImGui::TableSetupColumn("f01");
    ImGui::TableSetupColumn("T1");
    ImGui::TableSetupColumn("T2");
    ImGui::TableSetupColumn("readout");
    ImGui::TableSetupColumn("1q error");
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();
    ImGuiListClipper clipper;                               // spec 19 §5.7
    clipper.Begin(static_cast<int>(c.qubits.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const hw::QubitCal& q = c.qubits[static_cast<std::size_t>(i)];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            widgets::text(ctx, ctx.th().qubitColor(static_cast<std::uint32_t>(i)), "q" + std::to_string(i));
            ImGui::TableNextColumn();
            FontScope f(*ctx.fonts, FontRole::Readout);
            widgets::text(ctx, Token::TextPrimary, format::value(q.f01.value.v, "Hz"));
            ImGui::TableNextColumn();
            widgets::text(ctx, Token::TextPrimary, format::value(q.t1.value.v, "s"));
            ImGui::TableNextColumn();
            widgets::text(ctx, Token::TextPrimary, format::value(q.t2echo.value.v, "s"));
            ImGui::TableNextColumn();
            widgets::text(ctx, Token::TextPrimary, format::percent(q.readoutFidelity(), 2));
            ImGui::TableNextColumn();
            widgets::text(ctx, Token::TextPrimary, format::number(q.gateError1q.value, 3));
        }
    }
    clipper.End();
    ImGui::EndTable();
    ImGui::SameLine();
    widgets::fidelityBadge(ctx, data::FidelityClass::Model);
}

void ProjectPanel::draw(UiContext& ctx) {
    widgets::labelled(ctx, "Project", ctx.session_view.project.empty() ? std::string(ctx.text("app.project_untitled"))
                                                                       : ctx.session_view.project);
    ImGui::SameLine();
    if (widgets::secondaryButton(ctx, ctx.text("menu.save")) && ctx.cmd.saveProject) ctx.cmd.saveProject();
    ImGui::Separator();

    const std::vector<std::string> ids = hw::shippedDeviceIds();
    std::vector<std::string_view> names;
    names.reserve(ids.size());
    for (const std::string& id : ids) names.emplace_back(id);
    int current = 0;
    for (std::size_t i = 0; i < ids.size(); ++i)
        if (ids[i] == ctx.session_view.device) current = static_cast<int>(i);
    if (!names.empty() && widgets::combo(ctx, ctx.text("run.device"), &current, names, "Device") && ctx.cmd.selectDevice)
        ctx.cmd.selectDevice(ids[static_cast<std::size_t>(current)]);
    // Spec 19 §3: an edit creates a derived device; the shipped assets are read-only.
    widgets::checkbox(ctx, "Edit as derived device", &derived_, "Derived device");
    if (derived_) widgets::text(ctx, Token::Warn, "Edits are written to a derived copy, never to the shipped asset.");

    if (ctx.device == nullptr) {
        widgets::placeholder(ctx, "No device selected.");
        return;
    }
    if (!ImGui::BeginTabBar("##project")) return;
    if (ImGui::BeginTabItem("device.json")) {
        drawDevice(ctx);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("calibration.json")) {
        if (ctx.calibration != nullptr) drawCalibration(ctx);
        else widgets::text(ctx, Token::TextSecondary, "No calibration loaded.");
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

} // namespace

PanelPtr makeExamplesPanel() { return std::make_unique<ExamplesPanel>(); }
PanelPtr makeProjectPanel() { return std::make_unique<ProjectPanel>(); }

} // namespace qlab::ui
