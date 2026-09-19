// Spec 02 §5–§6 — one frame of the model: the lab clock, the fridge at 10 Hz, live instrument
// acquisitions, the two narrow records the instruments read (`instr::RunView` / `Environment`), the
// `viz::ViewInput` the state views consume, and the off-thread reduction job.
#include "App/Model.hpp"
#include <algorithm>
#include <atomic>
#include <future>

namespace qlab::app {
namespace {

// The snapshot the playhead is on: the last one at or before `gate` (spec 15 §3.6).
const runtime::RunSnapshot* snapshotAt(const runtime::RunResult& r, std::uint64_t gate) {
    const runtime::RunSnapshot* best = nullptr;
    for (const runtime::RunSnapshot& s : r.snapshots)
        if (s.gateIndex <= gate && (best == nullptr || s.gateIndex >= best->gateIndex))
            best = &s;
    if (best == nullptr && !r.snapshots.empty())
        best = &r.snapshots.front();
    return best;
}

// Where the playhead opens after a run. Not simply the last snapshot: a per-shot execution
// (spec 15 §3.5 (b)) projects its terminal measurements, so the last state is one shot's collapsed
// basis state — a number that changes with the seed and says nothing the histogram does not
// already say. The playhead therefore opens on the last snapshot taken before anything was
// measured, which is the state the program prepared; scrubbing forward shows the collapse.
std::uint64_t openingGate(const runtime::RunResult& r) {
    const auto unmeasured = [](const runtime::RunSnapshot& s) {
        for (std::int8_t bit : s.measuredBits)
            if (bit >= 0)
                return false;
        return true;
    };
    std::optional<std::uint64_t> best;
    for (const runtime::RunSnapshot& s : r.snapshots)
        if (unmeasured(s) && (!best || s.gateIndex > *best))
            best = s.gateIndex;
    if (best)
        return *best;
    return r.snapshots.empty() ? 0 : r.snapshots.back().gateIndex;
}

viz::math::MeasurementMap measurementMap(const runtime::RunResult& r) {
    viz::math::MeasurementMap map;
    const std::uint32_t n = static_cast<std::uint32_t>(r.qubits.size());
    map.basis.assign(n, 'Z'); // spec 15 §4: every measurement is in the computational basis
    map.bitOfQubit.assign(n, -1);
    for (const runtime::MeasuredBit& m : r.measurements)
        if (m.qubit < n && m.bit != ir::kNoBit)
            map.bitOfQubit[m.qubit] = static_cast<std::int32_t>(m.bit.index);
    return map;
}

std::shared_ptr<const viz::LindbladSeries>
lindbladSeries(const std::shared_ptr<const runtime::RunResult>& r) {
    if (!r || r->lindblad.empty())
        return nullptr;
    auto series = std::make_shared<viz::LindbladSeries>();
    series->levels = r->levels;
    series->sites =
        r->levels > 0 && !r->lindblad.front().populations.empty()
            ? static_cast<std::uint32_t>(r->lindblad.front().populations.size() / r->levels)
            : 0;
    series->samples = r->lindblad;
    series->cls = data::FidelityClass::Numerical;
    return series;
}

} // namespace

// ---------------------------------------------------------------- reductions

struct LabModel::ReductionJob {
    struct Payload {
        std::atomic<bool> done{false};
        std::shared_ptr<const viz::Reductions> result;
    };
    std::shared_ptr<Payload> payload = std::make_shared<Payload>();
    std::future<void> future;
    std::uint64_t gate = 0;
};

// Defined here because `ReductionJob` must be complete wherever a `LabModel` is created or
// destroyed.
LabModel::LabModel()
    : jobs_(&core::JobSystem::global()), live_state_(std::make_shared<LiveState>()),
      live_(instruments_, *jobs_, bus_), session_(&bus_, jobs_), incremental_(*jobs_) {}

LabModel::~LabModel() {
    // Spec 02 §6: cancel everything in flight before the subsystems the workers point at die.
    incremental_.cancel();
    live_.waitIdle();
    waitReductions();
}

void LabModel::requestReductions(const viz::ReductionRequest& request) {
    if (request.empty() || !live_state_->snapshot)
        return;
    if (reduction_ != nullptr)
        return; // one reduction at a time; the next frame asks again
    auto job = std::make_unique<ReductionJob>();
    job->gate = live_state_->snapshot->gateIndex;
    auto payload = job->payload;
    auto snapshot = live_state_->snapshot;
    viz::ReductionRequest merged = request;
    // Spec 17 §8: the lab's Bloch markers need the single-qubit reductions whether a view asks for
    // them or not; in Physical-lab mode nothing reads them, so they are not computed.
    if (!live_state_->physicalLab && lab_ != nullptr && !lab_->scene.qubitNodes().empty())
        merged.singles = true;
    job->future = jobs_->submit(
        [payload, snapshot, merged](std::stop_token stop) {
            if (auto r = viz::computeReductions(*snapshot, merged, stop))
                payload->result = std::make_shared<const viz::Reductions>(std::move(*r));
            payload->done.store(true, std::memory_order_release);
        },
        core::JobPriority::Interactive);
    reduction_ = std::move(job);
}

bool LabModel::reductionsInFlight() const {
    return reduction_ != nullptr && !reduction_->payload->done.load(std::memory_order_acquire);
}

void LabModel::collectReductions() {
    if (reduction_ == nullptr || !reduction_->payload->done.load(std::memory_order_acquire))
        return;
    if (reduction_->payload->result)
        live_state_->reductions = reduction_->payload->result;
    reduction_.reset();
}

void LabModel::waitReductions() {
    if (reduction_ == nullptr)
        return;
    if (reduction_->future.valid())
        reduction_->future.wait();
    collectReductions();
}

void LabModel::tickLab(double dtSeconds, gfx::Camera& camera) {
    if (tour_ != nullptr && tour_->playing())
        tour_->update(dtSeconds, camera);
    else if (lab_ != nullptr)
        lab_->interaction.update(dtSeconds, camera);
}

// ---------------------------------------------------------------- the run under the playhead

void LabModel::setResult(std::shared_ptr<const runtime::RunResult> r) {
    live_state_->result = std::move(r);
    live_state_->reductions.reset();
    const runtime::RunResult* run = live_state_->result.get();
    if (run == nullptr) {
        live_state_->snapshot.reset();
        live_state_->schedule.reset();
        return;
    }
    live_state_->seed = run->seed;
    live_state_->schedule = run->schedule;
    live_state_->lastRunMs = static_cast<double>(run->wallTime.count()) * 1e-6;
    live_state_->runWallTimeS = run->estimate.wallTime.valueS;
    setPlayhead(openingGate(*run));
    if (!live_state_->snapshot)
        live_state_->snapshot = run->finalState;
}

void LabModel::setCompiled(std::shared_ptr<const compiler::CompiledProgram> p) {
    live_state_->compiled = std::move(p);
}

void LabModel::setPlayhead(std::uint64_t gateIndex) {
    live_state_->playheadGate = gateIndex;
    const runtime::RunResult* run = live_state_->result.get();
    if (run == nullptr)
        return;
    const runtime::RunSnapshot* s = snapshotAt(*run, gateIndex);
    if (s == nullptr)
        return;
    live_state_->snapshot = s->state;
    live_state_->playheadS = s->timeS;
    live_state_->reductions.reset(); // the views mark themselves stale until the job returns
}

void LabModel::applyLinePowers(const cryo::LinePowers& powers) {
    live_state_->linePowers = powers;
    if (cryo_ != nullptr)
        cryo_->network.linePowers = powers;
}

// ---------------------------------------------------------------- per-frame publication

void LabModel::publishInputs() {
    LiveState& s = *live_state_;
    if (s.registry == nullptr)
        return;
    instr::RunView view;
    const runtime::RunResult* run = s.result.get();
    view.running = s.running;
    view.seed = s.seed;
    view.shot = s.shotsDone;
    view.shots = s.shotsTotal;
    view.gateCursor = s.playheadGate;
    view.wallTimeS = s.runWallTimeS;
    view.playheadS = s.playheadS;
    view.schedule = s.schedule;
    view.state = s.snapshot;
    if (s.snapshot) {
        view.nQubits = s.snapshot->nQubits;
        view.levels = s.snapshot->levels;
    }
    if (run != nullptr) {
        view.backend = run->backend;
        if (view.nQubits == 0)
            view.nQubits = static_cast<std::uint32_t>(run->qubits.size());
        if (const runtime::RunSnapshot* snap = snapshotAt(*run, s.playheadGate); snap != nullptr)
            view.measuredBits = snap->measuredBits;
        view.populations.reserve(run->lindblad.size());
        for (const qsim::TimeSample& t : run->lindblad)
            view.populations.push_back(instr::LevelSample{t.timeS, t.populations});
    }
    s.registry->inputs()->publish(std::move(view));

    instr::Environment env;
    env.device = s.device;
    env.calibration = s.calibration;
    env.wiring = s.wiring != nullptr ? viz::borrow(*s.wiring) : nullptr;
    env.catalogs = catalogs_;
    env.thermal = s.thermal;
    env.ghs = s.ghs;
    env.labTimeS = s.labTimeS;
    env.seed = s.seed != 0 ? s.seed : env.seed;
    s.registry->inputs()->publish(std::move(env));
    s.environment = s.registry->inputs()->environment();
}

void LabModel::refreshViewInput() {
    const LiveState& s = *live_state_;
    viz::ViewInput in;
    in.snapshot = s.snapshot;
    in.reductions = s.reductions;
    in.playheadGate = s.playheadGate;
    in.hasPlayhead = static_cast<bool>(s.snapshot);
    in.playheadS = s.playheadS;
    in.device = s.device;
    in.calibration = s.calibration;
    in.schedule = s.schedule;
    if (const std::shared_ptr<const compiler::CompiledProgram>& c = s.compiled; c) {
        in.circuits.stages[static_cast<std::size_t>(viz::CircuitStage::Source)] =
            std::shared_ptr<const ir::Circuit>(c, &c->source);
        in.circuits.stages[static_cast<std::size_t>(
            c->timing.empty() ? viz::CircuitStage::Routed : viz::CircuitStage::Scheduled)] =
            std::shared_ptr<const ir::Circuit>(c, &c->circuit);
        in.circuits.layout = c->initialLayout.v2p;
        in.circuits.finalLayout = c->finalLayout.v2p;
    }
    if (const std::shared_ptr<const runtime::RunResult>& r = s.result; r) {
        in.counts = std::shared_ptr<const data::Histogram>(r, &r->counts);
        if (r->exact)
            in.idealProbabilities = std::shared_ptr<const std::vector<double>>(r, &*r->exact);
        in.idealClass = r->exactClass;
        in.measurement = std::make_shared<const viz::math::MeasurementMap>(measurementMap(*r));
        if (r->circuit)
            in.circuits.stages[static_cast<std::size_t>(viz::CircuitStage::Scheduled)] = r->circuit;
        if (r->source)
            in.circuits.stages[static_cast<std::size_t>(viz::CircuitStage::Source)] = r->source;
        if (!r->initialLayout.empty())
            in.circuits.layout = r->initialLayout.v2p;
        if (!r->finalLayout.empty())
            in.circuits.finalLayout = r->finalLayout.v2p;
        in.lindblad = lindbladSeries(r);
        if (!in.schedule)
            in.schedule = r->schedule;
    }
    viewInput_ = std::move(in);
}

// Spec 22 §1 — the channels the Plots panel finds when the user opens it: the six stage
// temperatures and the load the mixing chamber is carrying, sampled at the fridge's 10 Hz.
void LabModel::recordChannels() {
    for (int i = 0; i < cryo::kStageCount; ++i) {
        const auto stage = static_cast<cryo::Stage>(i);
        data::ChannelDesc desc;
        desc.id = std::format("cryo.{}.T", cryo::stageName(stage));
        desc.unit = "K";
        desc.xUnit = "s";
        desc.cls = data::FidelityClass::Numerical;
        stageChannels_[static_cast<std::size_t>(i)] = recorder_.add(std::move(desc));
    }
    data::ChannelDesc load;
    load.id = "cryo.MXC.P_load";
    load.unit = "W";
    load.xUnit = "s";
    load.cls = data::FidelityClass::Model;
    mxcLoadChannel_ = recorder_.add(std::move(load));
}

void LabModel::recordSamples() {
    const LiveState& s = *live_state_;
    for (int i = 0; i < cryo::kStageCount; ++i) {
        const std::size_t k = static_cast<std::size_t>(i);
        (void)recorder_.push(stageChannels_[k], s.thermal.time_s, s.thermal.T_K[k]);
    }
    (void)recorder_.push(
        mxcLoadChannel_, s.thermal.time_s,
        s.thermal.load_W[static_cast<std::size_t>(cryo::stageIndex(cryo::Stage::MXC))]);
}

void LabModel::tick(double dtSeconds) {
    LiveState& s = *live_state_;
    s.labTimeS += std::max(0.0, dtSeconds);
    int fridgeSteps = 0;
    if (cryo_ != nullptr) {
        fridgeSteps = cryo_->advance(dtSeconds);
        s.thermal = cryo_->snapshot;
        s.ghs = cryo_->ghs.snapshot();
        s.fridge = cryo_->sequencer.state();
        s.cooling = cryo_->network.cooling;
        if (fridgeSteps > 0)
            recordSamples();
    }
    collectReductions();
    publishInputs();
    if (s.registry != nullptr)
        live_.tick(s.labTimeS);
    refreshViewInput();
    if (lab_ != nullptr)
        lab_->overlays.update(s.labTimeS);
}

} // namespace qlab::app
