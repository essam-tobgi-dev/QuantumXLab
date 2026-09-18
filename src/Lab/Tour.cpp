// Spec 17 §7.10 — the guided tour: script loading and validation (this half) and the transport,
// flight and dwell engine (below). Every unresolved focus or theory anchor is a load error: the
// script is part of the product and must not degrade silently.
#include "Lab/Tour.hpp"
#include "Core/Json.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <fstream>
#include <glm/gtc/constants.hpp>

namespace qlab::lab {
namespace {

// slugify / headingAnchor mirror the descriptor lint (CatalogLint.cpp, spec 25 §7): the anchor of
// "### 6.3 Dispersive readout" is "6.3-dispersive-readout".
std::string slugify(std::string_view text) {
    std::string out;
    bool pendingDash = false;
    for (char ch : text) {
        auto c = static_cast<unsigned char>(ch);
        if (c == '$') continue;
        if (c < 0x80 && std::isalnum(c)) {
            if (pendingDash && !out.empty()) out.push_back('-');
            pendingDash = false;
            out.push_back(static_cast<char>(std::tolower(c)));
        } else {
            pendingDash = true;
        }
    }
    return out;
}

std::optional<std::string> headingAnchor(const std::string& line) {
    if (line.rfind("## Where this is used", 0) == 0) return std::string("where-this-is-used");
    std::size_t hashes = 0;
    while (hashes < line.size() && line[hashes] == '#') ++hashes;
    if (hashes < 2 || hashes > 3) return std::nullopt;
    std::size_t i = hashes;
    if (i >= line.size() || !std::isspace(static_cast<unsigned char>(line[i]))) return std::nullopt;
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
    const std::size_t numStart = i;
    while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) ++i;
    if (i == numStart) return std::nullopt;
    if (i + 1 < line.size() && line[i] == '.' && std::isdigit(static_cast<unsigned char>(line[i + 1]))) {
        ++i;
        while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) ++i;
    }
    const std::string number = line.substr(numStart, i - numStart);
    if (i < line.size() && line[i] == '.') ++i;
    if (i >= line.size() || !std::isspace(static_cast<unsigned char>(line[i]))) return std::nullopt;
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
    std::string rest = line.substr(i);
    while (!rest.empty() && (rest.back() == '\r' || std::isspace(static_cast<unsigned char>(rest.back()))))
        rest.pop_back();
    return number + "-" + slugify(rest);
}

bool registered = false;
void registerTourKind() {
    if (registered) return;
    core::JsonEnvelope::registerKind("lab.tour", 1);
    registered = true;
}

Result<TourStep> parseStep(const core::Json& j, std::size_t index) {
    const std::string where = std::format("steps[{}]", index);
    if (!j.is_object()) return fail(kErrTour, where + " is not an object");
    TourStep s;
    const auto str = [&](const char* key, std::string& into, bool required) -> Status {
        const auto it = j.find(key);
        if (it == j.end()) return required ? fail(kErrTour, std::format("{}.{} is missing", where, key)) : Status{};
        if (!it->is_string()) return fail(kErrTour, std::format("{}.{} is not a string", where, key));
        into = it->get<std::string>();
        return {};
    };
    QXL_TRY(str("id", s.id, true));
    QXL_TRY(str("title", s.title, true));
    QXL_TRY(str("narration", s.narration, true));
    QXL_TRY(str("theory", s.theory, true));
    QXL_TRY(str("focus_instance", s.focusInstance, false));
    QXL_TRY(str("focus_descriptor", s.focusDescriptor, false));
    if (const auto it = j.find("bookmark"); it != j.end() && it->is_string()) s.bookmark = it->get<std::string>();
    if (s.focusInstance.empty() && s.focusDescriptor.empty() && !s.bookmark)
        return fail(kErrTour, where + " names neither focus_instance, focus_descriptor nor bookmark");
    if (s.narration.size() < 80) return fail(kErrTour, where + ".narration is too short to explain anything");
    s.dwell_s = j.value("dwell_s", s.dwell_s);
    s.orbitDegPerS = j.value("orbit_deg_per_s", s.orbitDegPerS);
    s.marginFactor = j.value("margin_factor", s.marginFactor);
    if (s.dwell_s <= 0.0 || s.marginFactor <= 0.0) return fail(kErrTour, where + ": dwell_s and margin_factor must be positive");
    if (const auto it = j.find("azimuth_deg"); it != j.end() && it->is_number()) s.azimuthDeg = it->get<double>();
    if (const auto it = j.find("elevation_deg"); it != j.end() && it->is_number()) s.elevationDeg = it->get<double>();
    if (const auto v = j.find("view"); v != j.end()) {
        if (!v->is_object()) return fail(kErrTour, where + ".view is not an object");
        if (const auto it = v->find("xray"); it != v->end() && it->is_boolean()) s.view.xray = it->get<bool>();
        if (const auto it = v->find("cutaway"); it != v->end() && it->is_boolean()) s.view.cutaway = it->get<bool>();
        if (const auto it = v->find("cans"); it != v->end() && it->is_boolean()) s.view.cans = it->get<bool>();
        if (const auto it = v->find("explode"); it != v->end() && it->is_number())
            s.view.explode = std::clamp(it->get<double>(), 0.0, 1.0);
        if (const auto it = v->find("layers"); it != v->end()) {
            if (!it->is_object()) return fail(kErrTour, where + ".view.layers is not an object");
            for (const auto& [name, on] : it->items()) {
                Group g;
                if (!groupFromName(name, g)) return fail(kErrTour, std::format("{}.view.layers: unknown layer '{}'", where, name));
                if (!on.is_boolean()) return fail(kErrTour, std::format("{}.view.layers.{} is not a boolean", where, name));
                s.view.layers.emplace_back(g, on.get<bool>());
            }
        }
    }
    if (const auto it = j.find("highlight"); it != j.end()) {
        if (!it->is_array()) return fail(kErrTour, where + ".highlight is not an array");
        for (const core::Json& h : *it) {
            if (!h.is_string()) return fail(kErrTour, where + ".highlight entries must be strings");
            s.highlight.push_back(h.get<std::string>());
        }
    }
    return s;
}

// Instance name first, then the first node of the descriptor (spec 17 §1 ids are not stable).
ComponentId resolve(const Scene& scene, std::string_view instance, std::string_view descriptor) {
    if (!instance.empty())
        if (ComponentId id = scene.findByInstance(instance); id.value != 0) return id;
    if (!descriptor.empty()) {
        const auto ids = scene.findByDescriptor(descriptor);
        if (!ids.empty()) return ids.front();
    }
    return ComponentId{0};
}

} // namespace

std::map<std::string, std::set<std::string>> theoryAnchorsIn(const std::filesystem::path& dir) {
    std::map<std::string, std::set<std::string>> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        const std::string fn = e.path().filename().string();
        if (fn.size() < 4 || fn[0] != 'T' || !std::isdigit(static_cast<unsigned char>(fn[1])) ||
            !std::isdigit(static_cast<unsigned char>(fn[2])) || fn[3] != '-')
            continue;
        auto& set = out[fn.substr(0, 3)];
        std::ifstream in(e.path());
        for (std::string line; std::getline(in, line);)
            if (auto a = headingAnchor(line)) set.insert(*a);
    }
    return out;
}

std::filesystem::path defaultTheoryDir() {
#if defined(QXL_SOURCE_DIR)
    const std::filesystem::path dir = std::filesystem::path(QXL_SOURCE_DIR) / "docs" / "theory";
    std::error_code ec;
    if (std::filesystem::is_directory(dir, ec)) return dir;
#endif
    return {};
}

Result<Tour> Tour::load(const std::filesystem::path& json, const Scene& scene, Interaction& ui,
                        const BindingRegistry* bindings, const std::filesystem::path& theoryDir) {
    registerTourKind();
    QXL_TRY_ASSIGN(const core::Envelope env, core::JsonEnvelope::load(json, "lab.tour"));
    const core::Json& d = env.data;
    if (!d.is_object()) return fail(kErrTour, json.string() + ": `data` is not an object");
    Tour t;
    t.scene_ = &scene;
    t.ui_ = &ui;
    t.id_ = d.value("id", json.stem().string());
    t.title_ = d.value("title", t.id_);
    const auto steps = d.find("steps");
    if (steps == d.end() || !steps->is_array() || steps->empty()) return fail(kErrTour, json.string() + ": `steps` is missing or empty");
    for (std::size_t i = 0; i < steps->size(); ++i) {
        QXL_TRY_ASSIGN(TourStep s, parseStep((*steps)[i], i));
        t.steps_.push_back(std::move(s));
    }

    // Every focus, highlight, bookmark and theory anchor resolves, or the whole script is refused.
    const std::filesystem::path corpus = theoryDir.empty() ? defaultTheoryDir() : theoryDir;
    const auto anchors = corpus.empty() ? std::map<std::string, std::set<std::string>>{} : theoryAnchorsIn(corpus);
    if (!theoryDir.empty() && anchors.empty()) return fail(kErrTour, "no theory documents under " + theoryDir.string());
    std::vector<std::string> problems;
    std::set<std::string> ids;
    for (std::size_t i = 0; i < t.steps_.size(); ++i) {
        const TourStep& s = t.steps_[i];
        if (!ids.insert(s.id).second) problems.push_back(std::format("step '{}' is listed twice", s.id));
        const ComponentId focus = resolve(scene, s.focusInstance, s.focusDescriptor);
        bool unresolved = false;
        if (focus.value == 0 && (!s.focusInstance.empty() || !s.focusDescriptor.empty())) {
            // Not an error: the device decides which lines and qubits exist (spec 17 §2).
            t.warnings_.push_back(std::format("step '{}' skipped: focus '{}' / '{}' is not in this scene", s.id,
                                              s.focusInstance, s.focusDescriptor));
            unresolved = true;
        }
        if (s.bookmark && ui.bookmark(*s.bookmark) == nullptr)
            problems.push_back(std::format("step '{}': bookmark '{}' does not exist", s.id, *s.bookmark));
        std::vector<ComponentId> extra;
        for (const std::string& h : s.highlight) {
            const ComponentId id = resolve(scene, h, h);
            if (id.value == 0) {
                t.warnings_.push_back(std::format("step '{}': highlight '{}' is not in this scene", s.id, h));
                continue;
            }
            else extra.push_back(id);
        }
        const std::size_t hash = s.theory.find('#');
        if (hash == std::string::npos || hash != 3 || s.theory[0] != 'T')
            problems.push_back(std::format("step '{}': theory anchor '{}' is not of the form Tnn#anchor", s.id, s.theory));
        else if (!anchors.empty()) {
            const auto doc = anchors.find(s.theory.substr(0, hash));
            if (doc == anchors.end() || !doc->second.contains(s.theory.substr(hash + 1)))
                problems.push_back(std::format("step '{}': unresolved theory anchor '{}'", s.id, s.theory));
        }
        const Node* n = scene.node(focus);
        const ComponentDescriptor* desc = n != nullptr ? scene.descriptor(*n) : nullptr;
        t.focus_.push_back(focus);
        t.highlight_.push_back(std::move(extra));
        t.simulatorOnly_.push_back(desc != nullptr && desc->simulatorOnly);
        t.unresolved_.push_back(unresolved);
    }
    if (!t.steps_.empty() && std::all_of(t.unresolved_.begin(), t.unresolved_.end(), [](bool u) { return u; }))
        problems.push_back("no step of the script resolves in this scene");
    if (!problems.empty()) {
        std::string msg = json.string() + ":";
        for (const std::string& p : problems) msg += "\n  " + p;
        return fail(kErrTour, msg);
    }
    t.probe_ = std::make_unique<Interaction>(ui.scene(), bindings);
    return t;
}

// ---------------------------------------------------------------- transport

void Tour::play() {
    if (steps_.empty()) return;
    if (phase_ == Phase::Idle) {
        savedView_ = ui_->saveViewState(); // restored exactly by stop() (spec 17 §7.10)
        if (finished_) step_ = 0;
        finished_ = false;
        if (stepSkipped(step_)) {
            const auto k = nextVisible(step_, +1);
            if (!k) return;
            step_ = *k;
        }
    }
    phase_ = Phase::Flying;
    pendingArrive_ = true;
    haveLast_ = false;
    t_ = 0.0;
}

void Tour::pause() {
    if (playing()) phase_ = Phase::Paused;
}

void Tour::userInterrupted() { pause(); }

void Tour::stop() {
    if (phase_ == Phase::Idle) return;
    ui_->loadViewState(savedView_);
    phase_ = Phase::Idle;
    pendingArrive_ = false;
    haveLast_ = false;
    t_ = 0.0;
}

void Tour::seek(std::size_t step) {
    if (steps_.empty()) return;
    step_ = std::min(step, steps_.size() - 1);
    finished_ = false;
    if (phase_ == Phase::Idle) {
        play();
        return;
    }
    phase_ = Phase::Flying;
    pendingArrive_ = true;
    t_ = 0.0;
}

void Tour::next() {
    if (const auto k = nextVisible(step_ + 1, +1)) {
        seek(*k);
        return;
    }
    // Past the last stop the tour is over: the view comes back, the last step stays selected.
    const bool wasActive = active();
    stop();
    finished_ = wasActive || finished_;
}

void Tour::prev() {
    if (step_ == 0) {
        seek(0);
        return;
    }
    if (const auto k = nextVisible(step_ - 1, -1)) seek(*k);
}

std::optional<std::size_t> Tour::nextVisible(std::size_t from, int direction) const {
    for (std::size_t k = from; k < steps_.size(); k = direction > 0 ? k + 1 : k - 1) {
        if (!stepSkipped(k)) return k;
        if (direction < 0 && k == 0) break;
    }
    return std::nullopt;
}

bool Tour::stepSkipped(std::size_t step) const {
    if (step < unresolved_.size() && unresolved_[step]) return true;
    return physicalLab_ && step < simulatorOnly_.size() && simulatorOnly_[step];
}

double Tour::stepProgress() const {
    if (steps_.empty()) return 0.0;
    const double total = kFlight_s + steps_[step_].dwell_s;
    switch (phase_) {
    case Phase::Flying: return std::clamp(t_ / total, 0.0, 1.0);
    case Phase::Dwelling: return std::clamp((kFlight_s + t_) / total, 0.0, 1.0);
    case Phase::Paused: return std::clamp(pausedProgress_, 0.0, 1.0);
    case Phase::Idle: break;
    }
    return finished_ ? 1.0 : 0.0;
}

// ---------------------------------------------------------------- flight and dwell

void Tour::update(double dt, gfx::Camera& camera) {
    if (!playing()) return;
    dt = std::max(0.0, dt);
    if (pendingArrive_) {
        arrive(camera);
        pendingArrive_ = false;
    } else if (haveLast_ && cameraMovedExternally(camera)) {
        pausedProgress_ = stepProgress();
        userInterrupted(); // spec 18 §6: any input stops the flight where it is
        return;
    }
    const TourStep& s = steps_[step_];
    if (phase_ == Phase::Flying) {
        camera.update(dt);
        t_ += dt;
        if (t_ + 1e-9 >= kFlight_s || !camera.transitioning()) {
            if (t_ + 1e-9 < kFlight_s) { // the flight was cancelled from outside, not completed
                pausedProgress_ = stepProgress();
                userInterrupted();
                return;
            }
            phase_ = Phase::Dwelling;
            t_ = 0.0;
        }
    } else if (phase_ == Phase::Dwelling) {
        t_ += dt;
        // The orbit takes pixel deltas at 0.005 rad/px (Camera::orbit); a slow drift around the focus.
        const double radians = glm::radians(s.orbitDegPerS) * dt;
        if (radians != 0.0) camera.orbit(radians / 0.005, 0.0);
        if (t_ >= s.dwell_s) {
            next();
            if (!playing()) return;
        }
    }
    fitClipPlanes(camera);
    remember(camera);
}

void Tour::remember(const gfx::Camera& camera) {
    lastCamera_ = camera.bookmark();
    haveLast_ = true;
}

bool Tour::cameraMovedExternally(const gfx::Camera& camera) const {
    const double scale = 1e-9 * (1.0 + camera.distance());
    return glm::length(camera.position() - lastCamera_.position) > scale ||
           glm::length(camera.target() - lastCamera_.target) > scale ||
           std::abs(camera.fovDeg() - lastCamera_.fovDeg) > 1e-9 || camera.ortho() != lastCamera_.ortho;
}

void Tour::arrive(gfx::Camera& camera) {
    const TourStep& s = steps_[step_];
    const ComponentId id = focus_[step_];
    phase_ = Phase::Flying;
    t_ = 0.0;
    if (id.value != 0) ui_->select(id); // the Inspector follows the tour (spec 17 §7.2)

    const auto frameFocus = [&]() -> gfx::Bookmark {
        gfx::Camera framed = camera;
        const Node* n = scene_->node(id);
        const gfx::Aabb box = n ? (n->subtreeBounds.valid() ? n->subtreeBounds : n->worldBounds) : emptyAabb();
        if (box.valid()) {
            if (s.azimuthDeg || s.elevationDeg) {
                const double a = glm::radians(s.azimuthDeg.value_or(35.0)), e = glm::radians(s.elevationDeg.value_or(20.0));
                const glm::dvec3 dir{std::cos(e) * std::sin(a), std::sin(e), std::cos(e) * std::cos(a)};
                framed.lookAt(box.center() + dir * std::max(box.radius(), 1e-6), box.center());
            } else if (std::abs(glm::dot(camera.forward(), glm::dvec3(0.0, 1.0, 0.0))) > 0.98) {
                framed.lookAt(box.center() + glm::dvec3(0.6, 0.35, 0.7) * std::max(box.radius(), 1e-6), box.center());
            }
            framed.frame(box, s.marginFactor); // spec 18 §6 framing, along the chosen direction
        }
        return framed.bookmark(s.title);
    };

    if (s.bookmark) {
        const LayoutBookmark* b = ui_->bookmark(*s.bookmark);
        target_ = b ? b->view : camera.bookmark();
        (void)ui_->applyBookmark(*s.bookmark, camera, kFlight_s); // chip bookmarks switch the layers
        applyView(foldedView(step_), target_);
    } else {
        target_ = frameFocus();
        applyView(foldedView(step_), target_);
        target_ = frameFocus();                 // the exploded view moves the bounds
        camera.transitionTo(target_, kFlight_s);
    }
    fitClipPlanes(camera);
    remember(camera);
}

TourStep::View Tour::foldedView(std::size_t upTo) const {
    TourStep::View out;
    for (std::size_t k = 0; k <= upTo && k < steps_.size(); ++k) {
        const TourStep::View& v = steps_[k].view;
        if (v.xray) out.xray = v.xray;
        if (v.cutaway) out.cutaway = v.cutaway;
        if (v.cans) out.cans = v.cans;
        if (v.explode) out.explode = v.explode;
        for (const auto& [group, on] : v.layers) {
            bool found = false;
            for (auto& [g, o] : out.layers)
                if (g == group) { o = on; found = true; }
            if (!found) out.layers.emplace_back(group, on);
        }
    }
    return out;
}

void Tour::applyView(const TourStep::View& v, const gfx::Bookmark& target) {
    if (v.xray) ui_->setXray(*v.xray);
    if (v.cans) ui_->setCansVisible(*v.cans);
    if (v.explode) ui_->setExplode(Assembly::FridgeStages, *v.explode);
    if (v.cutaway) {
        // The kept half-space is dot(n, p) + w >= 0 about the fridge axis (Interaction::cutawayPlane):
        // point n away from the camera so the half facing the viewer is the one cut away.
        glm::dvec3 toCamera = target.position - scene_->layout().fridgePosition_m;
        toCamera.y = 0.0;
        const double angle = glm::length(toCamera) > 1e-9 ? glm::degrees(std::atan2(-toCamera.z, -toCamera.x))
                                                         : ui_->cutawayAngleDeg();
        ui_->setCutaway(*v.cutaway, *v.cutaway ? angle : ui_->cutawayAngleDeg());
    }
    for (const auto& [group, on] : v.layers) ui_->setLayerVisible(group, on);
}

// ---------------------------------------------------------------- what the card shows

ComponentId Tour::focusId(std::size_t step) const { return step < focus_.size() ? focus_[step] : ComponentId{0}; }

std::string Tour::focusName() const {
    if (steps_.empty()) return {};
    if (const Node* n = scene_->node(focusId())) return n->displayName.empty() ? n->instanceName : n->displayName;
    return steps_[step_].bookmark.value_or(std::string{});
}

std::vector<Tooltip::LiveRow> Tour::liveRows() const {
    const ComponentId id = focusId();
    if (!probe_ || id.value == 0) return {};
    probe_->hover(id, 0.0);
    const auto tip = probe_->tooltip(1.0); // past the 250 ms hover delay (spec 17 §7.1)
    return tip ? tip->liveRows : std::vector<Tooltip::LiveRow>{};
}

} // namespace qlab::lab
