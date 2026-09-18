#pragma once
// Spec 12 §1 — the part every instrument shares: the validated settings store with clamp reports,
// the state machine, fault handling, bindings, and the acquisition wrapper that gives a model its
// inputs (settings snapshot, RunView, Environment, a seeded random stream, the time stamp).
//
// Threading (spec 12 §1: acquire() runs on a worker): settings and reports are guarded by a mutex;
// the state is atomic and `Acquiring` doubles as the busy flag, so one acquisition runs at a time
// and a model may keep caches without locking. bind() is a set-up call: bind before going live.
#include "Core/Random.hpp"
#include "Instruments/Instrument.hpp"
#include <atomic>
#include <deque>
#include <map>
#include <mutex>

namespace qlab::instr {

// Immutable copy of an instrument's settings for one acquisition.
class SettingValues {
public:
    SettingValues() = default;
    explicit SettingValues(std::map<std::string, SettingValue, std::less<>> v) : values_(std::move(v)) {}
    double real(std::string_view key) const;          // Real or Int as double; 0 when absent
    std::int64_t integer(std::string_view key) const; // Int (or rounded Real); 0 when absent
    bool flag(std::string_view key) const;            // Bool; false when absent
    std::string text(std::string_view key) const;     // Enum / Text; empty when absent
    bool has(std::string_view key) const { return values_.find(key) != values_.end(); }
    const std::map<std::string, SettingValue, std::less<>>& all() const { return values_; }

private:
    std::map<std::string, SettingValue, std::less<>> values_;
};

// What a model gets for one acquisition.
struct AcquireContext {
    SettingValues settings;
    std::shared_ptr<const RunView> run;         // may be null: no run bound
    std::shared_ptr<const Environment> env;     // may be null: nothing bound
    Timestamp stamp;
    core::Random rng;                           // seeded from (run/env seed, instrument id, sequence)
};

class InstrumentBase : public IInstrument {
public:
    InstrumentId id() const final { return id_; }
    std::span<const ChannelDesc> channels() const final { return channels_; }
    const SettingSchema& settings() const final { return schema_; }
    Result<void> set(std::string_view key, SettingValue value) final;
    SettingValue get(std::string_view key) const final;
    Result<Trace> acquire(ChannelId channel) final;
    void bind(const Bindings& bindings) override;
    State state() const final { return state_.load(); }

    std::string faultMessage() const final;
    std::vector<SettingReport> reports() const final;
    Result<void> execute(const Command& command) final;
    const Bindings& bindings() const final { return bindings_; }
    bool simulatorOnly() const override { return false; }
    // Default: the numeric value of the setting named `path`.
    std::optional<double> query(std::string_view path) const override;
    core::Json saveSettings() const final;
    Result<void> loadSettings(const core::Json& values) final;

    static constexpr std::size_t kMaxReports = 32;

protected:
    InstrumentBase(InstrumentId id, SettingSchema schema);

    // Replaces the channel list, numbering the channels in order (constructors and bind()).
    void setChannels(std::vector<ChannelDesc> channels);
    SettingValues snapshot() const;
    std::shared_ptr<const RunView> runView() const;
    std::shared_ptr<const Environment> environment() const;
    const SignalGraph* routing() const { return bindings_.routing; }

    // The model. Called in state Acquiring with no lock held.
    virtual Result<Trace> doAcquire(const ChannelDesc& channel, AcquireContext& ctx) = 0;
    // Settings conflict check (spec 12 §1): a non-empty message puts the instrument in Fault.
    virtual std::string conflict(const SettingValues& values) const;
    // True when the settings name a trigger source, which arms the instrument (Idle → Armed).
    // Default: a "trigger" setting other than "free_run".
    virtual bool triggerSourceSet(const SettingValues& values) const;
    // Hook after a value changed (invalidate caches). Called with the settings lock released.
    virtual void settingChanged(std::string_view key);

    // A trace pre-filled from the channel description and the context's time stamp.
    Trace makeTrace(const ChannelDesc& channel, const AcquireContext& ctx) const;
    // A one-point trace (lab time, value) for scalar readings such as "f" or "locked".
    Trace scalarTrace(const ChannelDesc& channel, const AcquireContext& ctx, double value) const;
    // Runtime fault raised by a model (AWG memory overflow): state → Fault with `message`.
    // Returns the error to hand back from doAcquire. `const` because the fault belongs to the
    // configuration, not to the caller: a const reader (ISignalSource::signal) that discovers an
    // unplayable configuration must latch it too (spec 12 §3).
    std::unexpected<Error> raiseFault(ErrorCode code, std::string message) const;
    // Stores a value bypassing the read-only rule (tools writing corrections). Still validated.
    Result<void> storeInternal(std::string_view key, SettingValue value);

private:
    void moveTo(State to) const;
    void revalidate();                  // conflict() → Fault / back to Idle
    Result<void> store(std::string_view key, SettingValue value, bool internal);

    InstrumentId id_;
    SettingSchema schema_;
    std::vector<ChannelDesc> channels_;
    Bindings bindings_;
    mutable std::mutex mu_;             // values_, reports_, fault_, sequence counters
    std::map<std::string, SettingValue, std::less<>> values_;
    std::deque<SettingReport> reports_;
    // The fault and the state machine are runtime state, not logical constness: a const reader can
    // latch a Fault (see raiseFault).
    mutable std::string fault_;         // settings conflict or runtime fault
    mutable bool runtimeFault_ = false;
    std::uint64_t reportSeq_ = 0;
    mutable std::uint64_t acquireSeq_ = 0;
    mutable std::atomic<State> state_{State::Off};
};

} // namespace qlab::instr
