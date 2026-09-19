#pragma once
// Spec 17 §7.10 — the guided tour: a scripted, narrated fly-through of the laboratory in signal
// order (room → racks → fridge → stages → chip → qubits → readout chain → rack). Entirely headless
// (layer 4: no ImGui, no GL): the engine owns the script, the transport state, the camera flights
// and the dwell drift; the UI only draws what it reports. Components are resolved by instance name
// or descriptor id, never by node index, so the scene geometry may change under the script.
#include "Core/Error.hpp"
#include "Graphics/Camera.hpp"
#include "Lab/Interaction.hpp"
#include "Lab/Scene.hpp"
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace qlab::lab {

inline constexpr ErrorCode kErrTour = ErrorCode::Lab_ + 7; // tour script invalid or unresolved

struct TourStep {
    std::string id, title, narration;           // narration: 3–6 sentences, physically exact
    std::string theory;                         // anchor "T08#1.2-cooling-power" (must resolve)
    std::string focusInstance, focusDescriptor; // component to frame (instance name wins)
    std::optional<std::string> bookmark;        // or a layout bookmark name
    double dwell_s = 8.0;                       // time on the stop before auto-advance
    double orbitDegPerS = 4.0;                  // slow drift around the focus while dwelling
    double marginFactor = 1.6;                  // framing (spec 18 §6) relative to the AABB
    std::optional<double> azimuthDeg,
        elevationDeg; // approach direction; absent: keep the current one
    struct View {
        std::optional<bool> xray, cutaway, cans;
        std::optional<double> explode; // Assembly::FridgeStages, 0..1
        std::vector<std::pair<Group, bool>> layers;
    } view;                             // applied on arrival
    std::vector<std::string> highlight; // extra instances outlined at this stop
};

class Tour {
  public:
    enum class Phase : std::uint8_t { Idle, Flying, Dwelling, Paused };
    static constexpr double kFlight_s = 0.9; // spec 17 §7.10: one eased flight per stop

    // `bindings` feeds the live rows (may be null: rows read "—"). `theoryDir` is where the anchors
    // are checked; empty means `QXL_SOURCE_DIR/docs/theory` when that exists (a source tree), and
    // no anchor check when it does not (an installed copy without the corpus).
    static Result<Tour> load(const std::filesystem::path& json, const Scene& scene, Interaction& ui,
                             const BindingRegistry* bindings = nullptr,
                             const std::filesystem::path& theoryDir = {});

    // ---- transport
    void play();
    void pause();
    void stop(); // restores the view state saved on play()
    void next();
    void prev();
    void seek(std::size_t step);
    // Flight (kFlight_s ease-in-out), then dwell with a slow orbit. Advances the camera itself;
    // the caller does not also run Interaction::update while the tour is playing.
    void update(double dt, gfx::Camera& camera);
    // Any camera input pauses the tour (spec 18 §6). `update` also detects a camera that moved
    // under it, so a viewport that does not call this still pauses the tour on input.
    void userInterrupted();

    // ---- state
    bool playing() const { return phase_ == Phase::Flying || phase_ == Phase::Dwelling; }
    bool paused() const { return phase_ == Phase::Paused; }
    bool active() const { return phase_ != Phase::Idle; }
    bool finished() const { return finished_; }
    Phase phase() const { return phase_; }
    std::size_t step() const { return step_; }
    std::size_t size() const { return steps_.size(); }
    double stepProgress() const; // 0..1 within the current step (flight + dwell)
    const TourStep& current() const { return steps_[step_]; }
    std::span<const TourStep> steps() const { return steps_; }
    const std::string& id() const { return id_; }
    const std::string& title() const { return title_; }
    // The node a step frames (0 when the step uses a bookmark only) and its extra outlines.
    ComponentId focusId(std::size_t step) const;
    ComponentId focusId() const { return focusId(step_); }
    std::string focusName() const; // display name of the current focus
    std::span<const ComponentId> highlightIds() const { return highlight_[step_]; }
    // Spec 19 §2 Physical-lab mode: steps whose focus is Simulator-only are skipped.
    void setPhysicalLab(bool on) { physicalLab_ = on; }
    bool physicalLab() const { return physicalLab_; }
    // A step is skipped when Physical-lab mode hides its focus, or when its focus is not in THIS
    // scene (a 27-qubit script visiting `line[39]` on a 5-qubit device): the script serves every
    // device of the layout, so a missing part is a warning, not a refusal. `warnings()` lists them.
    bool stepSkipped(std::size_t step) const;
    const std::vector<std::string>& warnings() const { return warnings_; }
    // The view state a stop is seen in is the FOLD of every stop up to it ("open the fridge" at
    // stop 4 holds for stop 11), so seeking lands in the same state as playing through.
    TourStep::View foldedView(std::size_t upTo) const;

    // Live rows for the narration card: the focus component's tooltip rows through the public
    // Interaction API (a private probe Interaction, so the viewport's hover state is untouched).
    std::vector<Tooltip::LiveRow> liveRows() const;

  private:
    Tour() = default;
    void arrive(gfx::Camera& camera); // start the flight to `step_` and apply its view
    void applyView(const TourStep::View& v, const gfx::Bookmark& target);
    std::optional<std::size_t> nextVisible(std::size_t from, int direction) const;
    bool cameraMovedExternally(const gfx::Camera& camera) const;
    void remember(const gfx::Camera& camera);

    const Scene* scene_ = nullptr;
    Interaction* ui_ = nullptr;
    std::unique_ptr<Interaction> probe_; // tooltip rows without touching the live hover
    std::string id_, title_;
    std::vector<TourStep> steps_;
    std::vector<ComponentId> focus_;
    std::vector<std::vector<ComponentId>> highlight_;
    std::vector<bool> simulatorOnly_;
    std::vector<bool> unresolved_; // focus/highlight absent from this scene
    std::vector<std::string> warnings_;
    Phase phase_ = Phase::Idle;
    std::size_t step_ = 0;
    double t_ = 0.0; // seconds into the current phase
    bool finished_ = false, physicalLab_ = false, pendingArrive_ = false;
    double pausedProgress_ = 0.0;       // stepProgress() frozen while paused
    core::Json savedView_;              // Interaction::saveViewState() at play()
    gfx::Bookmark target_, lastCamera_; // where the flight goes; what we left the camera at
    bool haveLast_ = false;
};

// The heading anchors of every `docs/theory/Tnn-*.md` under `dir`, keyed "Tnn", in the form the
// component descriptors use ("6.3-dispersive-readout"; spec 25 §7).
std::map<std::string, std::set<std::string>> theoryAnchorsIn(const std::filesystem::path& dir);
// Default corpus location: `QXL_SOURCE_DIR/docs/theory` (empty when it is not on disk).
std::filesystem::path defaultTheoryDir();

} // namespace qlab::lab
