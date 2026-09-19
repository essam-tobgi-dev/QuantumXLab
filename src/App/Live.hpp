#pragma once
// Spec 17 §5 — the one record every live value in the application is read from, and the six
// `lab::BindingRegistry` providers built on top of it.
//
// Lab depends on neither Runtime nor Instruments (spec 02 §1), so the App registers one provider
// per root namespace. Each provider holds a `shared_ptr<const LiveState>` — the same object the
// model refreshes once per frame on the UI thread — and looks nowhere else. A value flagged
// Simulator-only (spec 00 §6) returns `nullopt` while the Physical-lab toggle is on, which is what
// makes the overlay and the spec-sheet row show "—".
#include "Compiler/Pass.hpp"
#include "Cryo/Cryo.hpp"
#include "Hardware/Hardware.hpp"
#include "Instruments/Registry.hpp"
#include "Lab/BindingRegistry.hpp"
#include "Runtime/Run.hpp"
#include "Viz/Reductions.hpp"
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qlab::app {

// Everything the providers read. Plain data plus non-owning pointers into objects the model keeps
// alive for as long as the state: the cryogenic stack and the instrument registry.
struct LiveState {
    // ---- mode and clock
    bool physicalLab = false;
    double labTimeS = 0.0;

    // ---- cryogenics (owned by `CryoStack`)
    cryo::ThermalSnapshot thermal;
    cryo::GhsSnapshot ghs;
    cryo::FridgeState fridge = cryo::FridgeState::Warm;
    const cryo::Wiring* wiring = nullptr;
    const cryo::CoaxCatalog* coax = nullptr;
    const cryo::NoiseBudget* budget = nullptr;
    const cryo::HeatLoadModel* heat = nullptr;
    const cryo::MaterialCatalog* materials = nullptr;
    cryo::CoolingParams cooling;
    cryo::LinePowers linePowers;

    // ---- device
    std::shared_ptr<const hw::Device> device;
    std::shared_ptr<const hw::Calibration> calibration;

    // ---- the run the user is looking at
    std::shared_ptr<const runtime::RunResult> result;
    std::shared_ptr<const compiler::CompiledProgram> compiled;
    std::shared_ptr<const qsim::Snapshot> snapshot;    // state at the playhead (Simulator-only)
    std::shared_ptr<const viz::Reductions> reductions; // computed off the UI thread
    std::shared_ptr<const pulse::Schedule> schedule;
    bool running = false, compiling = false;
    std::uint64_t shotsDone = 0, shotsTotal = 0, seed = 0;
    std::uint64_t playheadGate = 0;
    double playheadS = 0.0;    // position inside `schedule`
    double runWallTimeS = 0.0; // hardware wall-time estimate consumed so far (spec 15 §6)
    double lastRunMs = 0.0;    // simulation wall time of the last finished run

    // ---- instruments (owned by the model)
    const instr::InstrumentRegistry* registry = nullptr;
    std::shared_ptr<const instr::Environment> environment;
    bool readoutActive = false; // a readout tone is on the feedline at the playhead

    // ---- helpers shared by the providers
    cryo::StageArray stageTemperatures() const;
    const cryo::WiringLine* line(std::size_t index) const;
    // Photon budget of an input line at `f` (spec 11 §6); nullopt without a wiring or a budget.
    std::optional<cryo::LineNoise> lineNoise(std::size_t index, double fHz) const;
    const viz::SingleReduction* single(std::uint32_t qubit) const;
    // Readout resonator frequency of a qubit (calibration first, then `device.readout`).
    std::optional<double> resonatorHz(std::uint32_t qubit) const;
};

// Registers `cryo.*`, `wiring.*`, `device.*`, `instr.*`, `run.*` and `static.*` on `registry`.
// `statics` serves `static.<key>` for constants that are not on the node itself (spec 17 §5).
void registerBindingProviders(lab::BindingRegistry& registry,
                              std::shared_ptr<const LiveState> state,
                              const lab::StaticProvider& statics);

// The individual providers, exposed so the test can exercise one at a time.
lab::BindingRegistry::Provider cryoProvider(std::shared_ptr<const LiveState> state);
lab::BindingRegistry::Provider wiringProvider(std::shared_ptr<const LiveState> state);
lab::BindingRegistry::Provider deviceProvider(std::shared_ptr<const LiveState> state);
lab::BindingRegistry::Provider instrProvider(std::shared_ptr<const LiveState> state);
lab::BindingRegistry::Provider runProvider(std::shared_ptr<const LiveState> state);

namespace detail {
// "line[3].attn[0].P_diss" → head "line", index 3, rest "attn[0].P_diss". A head without brackets
// leaves `index` unset. The same splitter the instrument registry uses for `instr.*` paths.
struct PathHead {
    std::string_view name;
    std::optional<std::uint32_t> index; // set when the bracket holds a number
    std::string_view key;               // the raw bracket text ("mxc" in "clamp[mxc].T")
    std::string_view rest;
};
PathHead splitPath(std::string_view path);
// Layout spelling ("mxc") or cryo spelling ("MXC"), case-insensitive.
bool stageFromAnyName(std::string_view name, cryo::Stage& out);
// Spec 17 §8 — the Illustrative signal-flow packet: "line[k].t_s" (seconds since the line's most
// recent play started), "line[k].amp" and "line[k].sigma_s".
std::optional<lab::BindingValue> packetBinding(const LiveState& state, std::string_view path);
// Signal power a line carries at its room-temperature bulkhead, in watts (spec 11 §5).
double lineInputPower(const LiveState& state, const cryo::WiringLine& line);
} // namespace detail

} // namespace qlab::app
