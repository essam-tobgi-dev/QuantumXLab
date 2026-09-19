// Spec 19 §3 "Viewport" — hosts the tone-mapped renderer texture (spec 18 §5) with the bookmark
// bar, layer toggles, exploded / cutaway / X-ray controls, the colormap legend and the dev FPS
// counter, and forwards mouse and keyboard input to `gfx::Camera` and `lab` picking.
#include "UI/Format.hpp"
#include "Data/Fidelity.hpp"
#include "UI/Panels/Panels.hpp"
#include "UI/Widgets/NumberField.hpp"
#include "UI/Widgets/Widgets.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <utility>
#include <glm/gtc/constants.hpp>
#include <imgui.h>

namespace qlab::ui {
namespace {

using widgets::iv;
using widgets::u32;

// Spec 19 §3 / brief: the cutaway must open TOWARD the viewer. The azimuth of the camera around the
// fridge axis (world +Y) is atan2(x, z) of the eye relative to the target; passing it to
// `Interaction::setCutaway` removes the half of every can that faces the camera.
double cameraAzimuthDeg(const gfx::Camera& camera) {
    const glm::dvec3 d = camera.position() - camera.target();
    return glm::degrees(std::atan2(d.x, d.z));
}

class ViewportPanel final : public BasicPanel {
public:
    ViewportPanel()
        : BasicPanel(PanelId::Viewport, "viewport", "panels.viewport", "▲", Workspace::Lab) {}

    void draw(UiContext& ctx) override;
    core::Json serialize() const override {
        core::Json j = core::Json::object();
        j["cutaway_offset_deg"] = cutawayOffsetDeg_;
        j["show_legend"] = showLegend_;
        j["show_help"] = showHelp_;
        j["invert_orbit"] = invertOrbit_;
        j["time_dilation"] = timeDilation_;
        return j;
    }
    void deserialize(const core::Json& j) override {
        if (!j.is_object()) return;
        if (const auto it = j.find("cutaway_offset_deg"); it != j.end() && it->is_number())
            cutawayOffsetDeg_ = it->get<double>();
        if (const auto it = j.find("show_legend"); it != j.end() && it->is_boolean()) showLegend_ = it->get<bool>();
        if (const auto it = j.find("show_help"); it != j.end() && it->is_boolean()) showHelp_ = it->get<bool>();
        if (const auto it = j.find("invert_orbit"); it != j.end() && it->is_boolean()) invertOrbit_ = it->get<bool>();
        if (const auto it = j.find("time_dilation"); it != j.end() && it->is_number())
            timeDilation_ = it->get<double>();
    }

private:
    void drawToolbar(UiContext& ctx);
    void drawImage(UiContext& ctx);
    void drawLegend(UiContext& ctx, ImVec2 imageMin, ImVec2 imageMax);

    void drawHoverCard(UiContext& ctx);
    void drawContextMenu(UiContext& ctx);
    void drawHelp(UiContext& ctx, ImVec2 imageMin, ImVec2 imageMax);
    void handleKeys(UiContext& ctx, bool pointerInside);

    double cutawayOffsetDeg_ = 0.0;   // user rotation on top of the camera azimuth
    double timeDilation_ = 1e7;       // spec 17 §8 pulse-packet slider
    bool showLegend_ = true;
    bool showHelp_ = false;
    bool invertOrbit_ = false;        // true: drag turns the camera instead of moving the scene
    ImVec2 lastImageSize_{0.0f, 0.0f};
    int stableFrames_ = 0;
    // The last asynchronous pick (spec 18 §5 pass 11): what is under the cursor and where it is.
    ComponentId hoverId_{0};
    bool hoverHit_ = false;
    glm::dvec3 hoverWorld_{0.0};
    bool pressedInside_ = false;      // the current left press started on the picture
    ComponentId contextId_{0};        // the part the context menu was opened on
};

void ViewportPanel::drawToolbar(UiContext& ctx) {
    lab::Interaction* ui = ctx.interaction;
    const Metrics m = ctx.metrics_px();
    if (ui == nullptr) {
        widgets::text(ctx, Token::TextSecondary, ctx.text("inspector.no_selection"));
        return;
    }
    // The toolbar wraps: an item that does not fit on the line goes to the next one, so a narrow
    // panel never clips a control (the `Time ×` field and the `?` chip used to fall off the edge).
    const float pad = ImGui::GetStyle().FramePadding.x;
    bool first = true;
    auto place = [&](float width, float gap = -1.0f) {
        if (first) { first = false; return; }
        ImGui::SameLine(0.0f, gap);
        if (ImGui::GetContentRegionAvail().x < width) ImGui::NewLine();
    };
    auto buttonWidth = [&](std::string_view label) { return widgets::textSize(label).x + 2.0f * pad; };

    // ---- bookmark bar (spec 17 §7: the layout's bookmarks plus one per qubit)
    int shown = 0;
    for (const lab::LayoutBookmark& b : ui->bookmarks()) {
        if (shown >= 9) break; // digits 1–9 recall them (spec 19 §5)
        const std::string label = std::to_string(shown + 1) + " " + b.name;
        place(buttonWidth(label));
        if (widgets::secondaryButton(ctx, label) && ctx.camera != nullptr) ui->applyBookmark(b.name, *ctx.camera);
        ++shown;
    }

    // ---- exploded / X-ray / cutaway (spec 17 §7.4 – §7.6)
    if (ctx.overlays != nullptr) { // spec 17 §8: the stage-temperature tint is opt-in
        bool temp = ctx.overlays->temperatureTint();
        place(buttonWidth("Temp"), m.spacing(4));
        if (widgets::toggleChip(ctx, "Temp", &temp)) ctx.overlays->setTemperatureTint(temp);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Colour the stages by their live temperature (inferno, 10 mK – 300 K)");
    }
    bool xray = ui->xray();
    place(buttonWidth("X-ray"), ctx.overlays != nullptr ? -1.0f : m.spacing(4));
    if (widgets::toggleChip(ctx, "X-ray", &xray)) ui->setXray(xray);
    bool cutaway = ui->cutaway();
    place(buttonWidth("Cutaway"));
    if (widgets::toggleChip(ctx, "Cutaway", &cutaway)) {
        const double azimuth = ctx.camera != nullptr ? cameraAzimuthDeg(*ctx.camera) : 0.0;
        ui->setCutaway(cutaway, azimuth + cutawayOffsetDeg_);
    }
    if (ui->cutaway()) {
        place(ctx.ui(90.0f) + buttonWidth("Cut"));
        double offset = glm::radians(cutawayOffsetDeg_);   // the field works in SI (rad), shows degrees
        if (widgets::numberField(ctx, "Cut", &offset,
                                 widgets::FieldSpec{.unit = "°",
                                                    .step = glm::radians(0.5),
                                                    .lo = -glm::pi<double>(),
                                                    .hi = glm::pi<double>(),
                                                    .digits = 3,
                                                    .cls = data::FidelityClass::Illustrative,
                                                    .undoLabel = "Cutaway angle",
                                                    .width = ctx.ui(90.0f)})) {
            cutawayOffsetDeg_ = glm::degrees(offset);
            const double azimuth = ctx.camera != nullptr ? cameraAzimuthDeg(*ctx.camera) : 0.0;
            ui->setCutaway(true, azimuth + cutawayOffsetDeg_);
        }
    }
    double explode = ui->explode(lab::Assembly::FridgeStages);
    place(ctx.ui(90.0f) + buttonWidth("Explode"));
    if (widgets::numberField(ctx, "Explode", &explode,
                             widgets::FieldSpec{.step = 0.004, .lo = 0.0, .hi = 1.0, .digits = 3,
                                                .cls = data::FidelityClass::Illustrative, .width = ctx.ui(90.0f)}))
        ui->setExplode(lab::Assembly::FridgeStages, explode);

    // ---- layer toggles (spec 17 §7.7)
    place(ctx.ui(100.0f), m.spacing(4));
    ImGui::SetNextItemWidth(ctx.ui(100.0f));
    if (ImGui::BeginCombo("##layers", "Layers")) {
        for (int g = 0; g < lab::kGroupCount; ++g) {
            const auto group = static_cast<lab::Group>(g);
            bool on = ui->layerVisible(group);
            const std::string name(lab::groupName(group));
            if (ImGui::Checkbox(name.c_str(), &on)) ui->setLayerVisible(group, on);
        }
        bool cans = ui->cansVisible();
        if (ImGui::Checkbox("cans", &cans)) ui->setCansVisible(cans);
        ImGui::EndCombo();
    }
    if (ctx.overlays != nullptr) {
        place(ctx.ui(120.0f) + buttonWidth("Time ×"));
        if (widgets::numberField(ctx, "Time ×", &timeDilation_,
                                 widgets::FieldSpec{.step = 1e5, .lo = 1.0, .hi = 1e10, .digits = 3,
                                                    .cls = data::FidelityClass::Illustrative, .width = ctx.ui(120.0f)}))
            ctx.overlays->setTimeDilation(timeDilation_);
    }
    place(buttonWidth("?"));
    widgets::toggleChip(ctx, "?", &showHelp_);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mouse and keyboard controls");
    if (ctx.devMode) {
        place(ctx.ui(80.0f), m.spacing(4));
        widgets::text(ctx, Token::TextSecondary,
                      format::number(static_cast<double>(ImGui::GetIO().Framerate), 4) + " FPS");
    }
}

void ViewportPanel::drawImage(UiContext& ctx) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    if (avail.x < 4.0f || avail.y < 4.0f) return;
    const ImVec2 imageMax(origin.x + avail.x, origin.y + avail.y);

    // Spec 18 §5 / 19 §4: the FBO is recreated once the panel size has been stable for one frame,
    // so a drag that resizes the panel reallocates once at the end rather than on every frame.
    if (std::fabs(avail.x - lastImageSize_.x) > 0.5f || std::fabs(avail.y - lastImageSize_.y) > 0.5f) {
        lastImageSize_ = avail;
        stableFrames_ = 0;
    } else if (++stableFrames_ == 1 && ctx.cmd.resizeViewport) {
        ctx.cmd.resizeViewport(static_cast<int>(avail.x * ctx.dpiScale), static_cast<int>(avail.y * ctx.dpiScale));
    }

    // The picture is drawn straight into the draw list and an invisible BUTTON is laid over it:
    // `ImGui::Image` never becomes the active item, so a drag on it moved (and undocked) the panel
    // instead of the camera. The button claims all three mouse buttons for the camera.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, imageMax, u32(ctx.th()[Token::BgViewport]));
    if (ctx.renderer != nullptr) // origin bottom-left in GL, top-left in ImGui
        dl->AddImage(static_cast<ImTextureID>(ctx.renderer->colorTexture().id()), origin, imageMax, ImVec2(0.0f, 1.0f),
                     ImVec2(1.0f, 0.0f));
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##viewport_input", avail,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;

    // ---- picking (spec 18 §5 pass 11): the result of the pick queued two frames ago, then the
    // request for this frame. Both are free of GPU stalls.
    if (ctx.renderer != nullptr) {
        if (const auto r = ctx.renderer->pollPick()) {
            hoverId_ = r->id;
            hoverHit_ = r->hit;
            hoverWorld_ = r->world;
        }
        if (hovered)
            ctx.renderer->queuePick(static_cast<int>((mouse.x - origin.x) * ctx.dpiScale),
                                    static_cast<int>((mouse.y - origin.y) * ctx.dpiScale));
    }
    if (ctx.interaction != nullptr) ctx.interaction->hover(hovered ? hoverId_ : ComponentId{0}, ctx.timeS);

    // ---- camera (spec 18 §6): orbit about the point under the cursor, pan, zoom toward the
    // cursor, look-around; any input cancels a flight to a bookmark.
    if (gfx::Camera* cam = ctx.camera; cam != nullptr) {
        cam->setAspect(static_cast<double>(avail.x) / std::max(1.0f, avail.y));
        for (int b = 0; b < 3; ++b) {
            if (!hovered || !ImGui::IsMouseClicked(b)) continue;
            cam->cancelTransition();
            // The pivot is what was under the cursor at the press, so the orbit turns around it
            // and pan/zoom speeds follow its distance — the chip and the room share one camera.
            if (hoverHit_) cam->setPivot(hoverWorld_);
            if (b == ImGuiMouseButton_Left) pressedInside_ = true;
        }
        if (active) {
            const ImVec2 d = io.MouseDelta;
            const bool left = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            const bool pan = ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                             (left && io.KeyShift);
            if (pan) cam->pan(d.x, d.y, static_cast<int>(avail.y));
            else if (left && io.KeyAlt) cam->rotateInPlace(d.x, d.y);
            // The convention the user settled on after trying both: horizontally the camera turns
            // with the drag (drag right → look further right), vertically the scene follows the
            // mouse (drag down → what is under the cursor moves down). "Invert orbit" flips both.
            else if (left) cam->orbit(invertOrbit_ ? -d.x : d.x, invertOrbit_ ? d.y : -d.y);
        }
        if (hovered && io.MouseWheel != 0.0f) {
            cam->cancelTransition();
            cam->dollyToward(hoverHit_ ? hoverWorld_ : cam->target(), io.MouseWheel);
        }
        // Double-click focuses the component (spec 17 §7.3); a plain click selects it. A press that
        // travelled more than a few points was a drag, not a click.
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && ctx.interaction != nullptr) {
            if (hoverId_.value != 0) ctx.interaction->focus(hoverId_, *cam);
            pressedInside_ = false;
        }
        lab::fitClipPlanes(*cam);
    }
    // Right-click without a drag: the context menu of the part under the cursor (spec 17 §7.5).
    if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && ctx.interaction != nullptr) {
        const ImVec2 from = io.MouseClickedPos[ImGuiMouseButton_Right];
        if (std::hypot(mouse.x - from.x, mouse.y - from.y) < ctx.ui(4.0f)) {
            contextId_ = hoverId_;
            ImGui::OpenPopup("##viewport_context");
        }
    }
    drawContextMenu(ctx);
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        const ImVec2 from = io.MouseClickedPos[ImGuiMouseButton_Left];
        const float travel = std::hypot(mouse.x - from.x, mouse.y - from.y);
        if (pressedInside_ && hovered && travel < ctx.ui(4.0f) && ctx.interaction != nullptr) {
            ctx.interaction->select(hoverId_);
            if (ctx.cmd.selectComponent) ctx.cmd.selectComponent(hoverId_);
        }
        pressedInside_ = false;
    }
    handleKeys(ctx, hovered);
    if (hovered && !active) drawHoverCard(ctx);
    if (showLegend_ && ctx.overlays != nullptr && ctx.overlays->temperatureTint()) drawLegend(ctx, origin, imageMax);
    if (showHelp_) drawHelp(ctx, origin, imageMax);
    if (ctx.tour != nullptr) drawTourOverlay(ctx, origin, imageMax);
}

void ViewportPanel::handleKeys(UiContext& ctx, bool pointerInside) {
    if (ctx.interaction == nullptr || ctx.camera == nullptr) return;
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !pointerInside) return;
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    gfx::Camera& cam = *ctx.camera;
    // Spec 19 §5: viewport-focused digits recall bookmarks, Ctrl+digit stores them.
    if (const int digit = pollBookmarkDigit(io.WantTextInput); digit != 0) {
        const auto& marks = ctx.interaction->bookmarks();
        const auto n = static_cast<std::size_t>(std::abs(digit));
        if (digit < 0) ctx.interaction->storeBookmark("user " + std::to_string(n), cam);
        else if (n <= marks.size()) ctx.interaction->applyBookmark(marks[n - 1].name, cam);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F, false)) ctx.interaction->focus(ctx.interaction->selected(), cam);
    if (ImGui::IsKeyPressed(ImGuiKey_X, false)) ctx.interaction->setXray(!ctx.interaction->xray());
    if (ImGui::IsKeyPressed(ImGuiKey_C, false)) {
        const bool on = !ctx.interaction->cutaway();
        ctx.interaction->setCutaway(on, cameraAzimuthDeg(cam) + cutawayOffsetDeg_);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_E, false) && !io.KeyShift) // Shift+E flies up
        ctx.interaction->setExplode(lab::Assembly::FridgeStages,
                                    ctx.interaction->explode(lab::Assembly::FridgeStages) > 0.5 ? 0.0 : 1.0);
    if (ImGui::IsKeyPressed(ImGuiKey_T, false) && ctx.cmd.openTheory) {
        if (const auto tip = ctx.interaction->tooltip(1e300); tip && !tip->theory.empty()) ctx.cmd.openTheory(tip->theory);
    }
    // Fly (spec 18 §6): W/S along the view, A/D across it, Q/E down/up. The speed follows the
    // distance to the pivot, so the same keys cross the room and creep over the chip.
    glm::dvec3 move{0.0};
    if (ImGui::IsKeyDown(ImGuiKey_W)) move.z += 1.0;
    if (ImGui::IsKeyDown(ImGuiKey_S)) move.z -= 1.0;
    if (ImGui::IsKeyDown(ImGuiKey_D)) move.x += 1.0;
    if (ImGui::IsKeyDown(ImGuiKey_A)) move.x -= 1.0;
    if (ImGui::IsKeyDown(ImGuiKey_E) && io.KeyShift) move.y += 1.0;
    if (ImGui::IsKeyDown(ImGuiKey_Q)) move.y -= 1.0;
    if (move != glm::dvec3(0.0) && !io.KeyCtrl && !io.KeySuper) {
        cam.cancelTransition();
        const double speed = cam.distance() * (io.KeyShift ? 4.0 : 1.2);
        cam.fly(glm::normalize(move), std::max(0.0, ctx.deltaS), speed);
    }
}

// "T08#1.2-cooling-power" → "T08 §1.2"
std::string anchorLabel(std::string_view anchor) {
    const std::size_t hash = anchor.find('#');
    if (hash == std::string_view::npos) return std::string(anchor);
    std::string_view rest = anchor.substr(hash + 1);
    std::size_t n = 0;
    while (n < rest.size() && (std::isdigit(static_cast<unsigned char>(rest[n])) || rest[n] == '.')) ++n;
    if (n == 0) return std::string(anchor.substr(0, hash));
    return std::string(anchor.substr(0, hash)) + " §" + std::string(rest.substr(0, n));
}

void ViewportPanel::drawContextMenu(UiContext& ctx) {
    if (!ImGui::BeginPopup("##viewport_context")) return;
    lab::Interaction& ui = *ctx.interaction;
    const lab::Node* node = ctx.scene != nullptr && contextId_.value != 0 ? ctx.scene->node(contextId_) : nullptr;
    if (node != nullptr) {
        widgets::text(ctx, Token::TextSecondary, node->displayName);
        ImGui::Separator();
        if (node->can) {
            const bool open = !ui.cansVisible();
            if (ImGui::MenuItem(open ? "Close the cans" : "Open — show the interior")) ui.setCansVisible(open);
        }
        if (ImGui::MenuItem("Inspect")) {
            ui.select(contextId_);
            if (ctx.cmd.selectComponent) ctx.cmd.selectComponent(contextId_);
        }
        if (ImGui::MenuItem("Fly to") && ctx.camera != nullptr) ui.focus(contextId_, *ctx.camera);
        if (const auto tip = ui.tooltip(1e300); tip && tip->id == contextId_ && !tip->theory.empty())
            if (ImGui::MenuItem(("Theory " + anchorLabel(tip->theory)).c_str()) && ctx.cmd.openTheory) ctx.cmd.openTheory(tip->theory);
        ImGui::Separator();
    }
    bool cans = ui.cansVisible();
    if (ImGui::MenuItem("Cans visible", nullptr, &cans)) ui.setCansVisible(cans);
    bool cut = ui.cutaway();
    if (ImGui::MenuItem("Cutaway", "C", &cut) && ctx.camera != nullptr) ui.setCutaway(cut, cameraAzimuthDeg(*ctx.camera) + cutawayOffsetDeg_);
    bool xray = ui.xray();
    if (ImGui::MenuItem("X-ray", "X", &xray)) ui.setXray(xray);
    bool exploded = ui.explode(lab::Assembly::FridgeStages) > 0.5;
    if (ImGui::MenuItem("Exploded stages", "E", &exploded)) ui.setExplode(lab::Assembly::FridgeStages, exploded ? 1.0 : 0.0);
    ImGui::Separator();
    ImGui::MenuItem("Invert orbit", nullptr, &invertOrbit_);
    ImGui::EndPopup();
}

// Spec 17 §7.1 — the hover card: what the part is, what it is for, what it reads right now.
void ViewportPanel::drawHoverCard(UiContext& ctx) {
    if (ctx.interaction == nullptr) return;
    const std::optional<lab::Tooltip> tip = ctx.interaction->tooltip(ctx.timeS);
    if (!tip) return;
    const Metrics m = ctx.metrics_px();
    const float width = ctx.ui(380.0f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(width + 2.0f * m.spacing(3), FLT_MAX));
    if (!ImGui::BeginTooltip()) return;
    ImGui::PushTextWrapPos(width);
    {
        FontScope f(*ctx.fonts, FontRole::PanelTitle);
        widgets::text(ctx, Token::TextPrimary, tip->displayName.empty() ? tip->name : tip->displayName);
    }
    if (!tip->name.empty() && tip->name != tip->displayName) {
        ImGui::SameLine();
        widgets::text(ctx, Token::TextSecondary, tip->name);
    }
    if (!tip->category.empty()) widgets::badge(ctx, tip->category, ctx.th()[Token::AccentSoft]);
    if (tip->simulatorOnly) {
        ImGui::SameLine();
        widgets::simOnlyBadge(ctx);
    }
    if (!tip->text.empty()) widgets::textWrapped(ctx, Token::TextPrimary, tip->text);
    if (!tip->function.empty()) {
        ImGui::Spacing();
        widgets::textWrapped(ctx, Token::TextSecondary, tip->function);
    }
    if (!tip->liveRows.empty()) {
        ImGui::Spacing();
        ImGui::Separator();
        for (const lab::Tooltip::LiveRow& row : tip->liveRows) {
            {
                FontScope f(*ctx.fonts, FontRole::Code);
                widgets::text(ctx, Token::TextPrimary, row.text);
            }
            ImGui::SameLine();
            widgets::fidelityBadge(ctx, row.cls);
        }
    }
    ImGui::Spacing();
    ImGui::Separator();
    std::string footer = "Click: inspect  ·  Double-click: focus";
    if (const lab::Node* n = ctx.scene != nullptr ? ctx.scene->node(tip->id) : nullptr; n != nullptr && n->can)
        footer = "Right-click: open the can  ·  " + footer;
    if (!tip->theory.empty()) footer += "  ·  T: theory " + anchorLabel(tip->theory);
    widgets::text(ctx, Token::TextDisabled, footer);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

void ViewportPanel::drawHelp(UiContext& ctx, ImVec2 imageMin, ImVec2 imageMax) {
    static constexpr std::pair<const char*, const char*> kRows[]{
        {"Drag", "orbit around the point under the cursor (right-click to invert)"},
        {"Right-drag / Shift-drag", "pan"},
        {"Wheel", "zoom toward the cursor"},
        {"Alt-drag", "look around"},
        {"W A S D  Q E", "fly (Shift: faster)"},
        {"Click / double-click", "inspect / focus"},
        {"Right-click", "open or close the cans, cutaway, X-ray"},
        {"1 – 9", "camera bookmarks (Ctrl: store)"},
        {"F  X  C  E  T", "focus · X-ray · cutaway · explode · theory"},
    };
    const Metrics m = ctx.metrics_px();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float pad = m.spacing(3), lineH = ImGui::GetTextLineHeight() + m.spacing(1);
    float keyW = 0.0f;
    for (const auto& [key, what] : kRows) keyW = std::max(keyW, widgets::textSize(key).x);
    const float boxW = keyW + ctx.ui(250.0f) + 3.0f * pad;
    const float boxH = static_cast<float>(std::size(kRows)) * lineH + 2.0f * pad;
    const ImVec2 a(imageMin.x + m.spacing(4), imageMax.y - boxH - m.spacing(4));
    const ImVec2 b(a.x + boxW, a.y + boxH);
    Color bg = ctx.th()[Token::BgPanel];
    bg.a = 0.88f;
    dl->AddRectFilled(a, b, u32(bg), m.radiusMd);
    dl->AddRect(a, b, u32(ctx.th()[Token::Border]), m.radiusMd);
    float y = a.y + pad;
    for (const auto& [key, what] : kRows) {
        dl->AddText(ImVec2(a.x + pad, y), u32(ctx.th()[Token::TextPrimary]), key);
        dl->AddText(ImVec2(a.x + 2.0f * pad + keyW, y), u32(ctx.th()[Token::TextSecondary]), what);
        y += lineH;
    }
}

void ViewportPanel::drawLegend(UiContext& ctx, ImVec2 imageMin, ImVec2 imageMax) {
    if (ctx.overlays == nullptr) return;
    // Spec 17 §8 / 22 §4: no scalar is encoded by colour without a legend.
    const lab::ColormapLegend& legend = ctx.overlays->temperatureLegend();
    const Metrics m = ctx.metrics_px();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ctx.ui(14.0f), h = ctx.ui(110.0f);
    const ImVec2 a(imageMax.x - w - m.spacing(4), imageMin.y + m.spacing(4));
    for (int i = 0; i < 48; ++i) {
        const float t0 = static_cast<float>(i) / 48.0f, t1 = static_cast<float>(i + 1) / 48.0f;
        const glm::vec3 c = viz::math::sequentialColor(1.0 - static_cast<double>(t0));
        dl->AddRectFilled(ImVec2(a.x, a.y + h * t0), ImVec2(a.x + w, a.y + h * t1),
                          u32(Color(c, 1.0f)));
    }
    dl->AddRect(a, ImVec2(a.x + w, a.y + h), u32(ctx.th()[Token::Border]), m.radiusSm);
    const std::string hi = format::value(legend.max, "K");
    const std::string lo = format::value(legend.min, "K");
    const ImU32 label = u32(Theme::readableText(ctx.th()[Token::TextSecondary], ctx.th()[Token::BgViewport]));
    dl->AddText(ImVec2(a.x - widgets::textSize(hi).x - 4.0f, a.y), label, hi.c_str());
    dl->AddText(ImVec2(a.x - widgets::textSize(lo).x - 4.0f, a.y + h - ImGui::GetTextLineHeight()), label, lo.c_str());
}

void ViewportPanel::draw(UiContext& ctx) {
    drawToolbar(ctx);
    ImGui::Separator();
    drawImage(ctx);
}

} // namespace

PanelPtr makeViewportPanel() { return std::make_unique<ViewportPanel>(); }

} // namespace qlab::ui
