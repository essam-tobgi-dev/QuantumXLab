#pragma once
// Spec 12 §1 — the instrument interface, §11 bindings, and the command record every front-panel
// control maps to (spec 19 §3).
//
// The first block of IInstrument is the §1 interface verbatim. The second block is what the §1
// prose requires but its sketch has no member for: the Fault message, the clamp reports, power and
// arming (the state machine's inputs), and the scalar `query` the App adapts into `instr.*`
// binding providers (Lab must not depend on Instruments).
#include "Core/EventBus.hpp"
#include "Instruments/Inputs.hpp"
#include "Instruments/Settings.hpp"
#include "Instruments/Signal.hpp"
#include "Pulse/Channel.hpp"
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace qlab::instr {

// Spec 12 §11. `lab::ComponentId` is `qlab::ComponentId`; a fridge line is named by its
// `cryo::WiringLine::id`; the two snapshot subscriptions are the `InputHub`.
struct Bindings {
    ComponentId component{0};               // the rack unit / sensor in the 3D scene
    std::vector<std::string> lines;         // fridge lines this instrument touches
    std::vector<pulse::ChannelId> channels; // schedule channels it sources / sinks, in port order
    std::shared_ptr<const InputHub> inputs; // RunView + Environment snapshots
    core::EventBus* bus = nullptr;          // clamp reports, faults and live traces are posted here
    const SignalGraph* routing = nullptr;   // signal routing matrix, owned by the registry
};

// Every knob, button and menu entry of an instrument panel is one of these (spec 19 §3).
struct Command {
    enum class Kind : std::uint8_t { Set, PowerOn, PowerOff, Arm, Disarm, ClearFault, Preset };
    Kind kind = Kind::Set;
    std::string key;    // Set
    SettingValue value; // Set
    static Command set(std::string key, SettingValue v) { return {Kind::Set, std::move(key), std::move(v)}; }
    static Command of(Kind k) { return {k, {}, {}}; }
};

class IInstrument {
public:
    // ---- spec 12 §1
    virtual InstrumentId id() const = 0;
    virtual std::span<const ChannelDesc> channels() const = 0;
    virtual const SettingSchema& settings() const = 0;
    // Validates against the schema. An out-of-range value is clamped, stored and reported
    // (`reports()`, `SettingClamped` on the bus): the call succeeds. Errors are an unknown key or a
    // value of the wrong type. A settings conflict puts the instrument in Fault; the call succeeds.
    virtual Result<void> set(std::string_view key, SettingValue value) = 0;
    virtual SettingValue get(std::string_view key) const = 0; // monostate for an unknown key
    virtual Result<Trace> acquire(ChannelId channel) = 0;     // single-shot, synchronous, any thread
    virtual void bind(const Bindings& bindings) = 0;
    virtual State state() const = 0;
    virtual ~IInstrument() = default;

    // ---- required by the §1 prose
    virtual std::string faultMessage() const = 0;             // empty unless state() == Fault
    virtual std::vector<SettingReport> reports() const = 0;   // most recent clamps, oldest first
    virtual Result<void> execute(const Command& command) = 0; // power, arm, clear fault, preset, set
    virtual const Bindings& bindings() const = 0;
    virtual bool simulatorOnly() const = 0;                   // spec 12 §12 probes
    // Scalar readings for `instr.*` binding paths, relative to this instrument: "f", "on",
    // "ch[2].I", "lo_leak". nullopt when the path is not one of its readings.
    virtual std::optional<double> query(std::string_view path) const = 0;
    // Current settings as {"key": value} and back (project save, spec 23). Loading reports clamps.
    virtual core::Json saveSettings() const = 0;
    virtual Result<void> loadSettings(const core::Json& values) = 0;
};

// Channel lookup by descriptor name ("s21", "ch[0].iq").
std::optional<ChannelId> findChannel(const IInstrument& instrument, std::string_view name);
Result<Trace> acquire(IInstrument& instrument, std::string_view channelName);

// ---- events posted on the bound bus (core::EventBus::post, drained on the main thread)
struct SettingClamped { SettingReport report; };
struct InstrumentFault { InstrumentId instrument; std::string message; };
struct StateChanged { InstrumentId instrument; State from, to; };
struct TraceReady { std::shared_ptr<const Trace> trace; }; // live mode (spec 12 §1)

} // namespace qlab::instr
