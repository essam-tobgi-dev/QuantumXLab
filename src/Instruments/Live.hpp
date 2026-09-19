#pragma once
// Spec 12 §1, §11 — live mode: acquire() of every live channel at the instrument's `refresh_hz`
// (default 10), run as jobs on the core::JobSystem, with each trace posted on the event bus as
// `TraceReady` (drained on the main thread). Pacing follows the lab clock handed to tick(), never
// the wall clock, so a replay with the same clock sequence acquires the same traces.
#include "Core/JobSystem.hpp"
#include "Instruments/Registry.hpp"
#include <atomic>
#include <future>

namespace qlab::instr {

struct AcquireFailed { // posted instead of TraceReady when a live acquisition fails
    InstrumentId instrument;
    std::string channel;
    Error error;
};

class LiveRunner {
  public:
    LiveRunner(InstrumentRegistry& registry, core::JobSystem& jobs, core::EventBus& bus);
    ~LiveRunner(); // waits for the acquisitions in flight

    // Turns live mode on or off for one channel. Errors: UnknownInstrument, UnknownChannel.
    Result<void> enable(const InstrumentId& id, std::string_view channel, bool on = true);
    bool enabled(const InstrumentId& id, std::string_view channel) const;
    std::size_t liveChannels() const { return entries_.size(); }

    // Call once per frame with the lab clock. Submits an acquisition for every live channel whose
    // period 1/refresh_hz has elapsed and that has none in flight; an instrument with a trigger
    // source set is re-armed first (spec 12 §1: Armed → Acquiring → Idle per acquisition). A
    // switched-off instrument is skipped. Returns the number of acquisitions submitted.
    std::size_t tick(double labTimeS);
    // Blocks until every submitted acquisition has finished (tests, shutdown).
    void waitIdle();
    std::size_t inFlight() const;

  private:
    struct Entry {
        IInstrument* instrument = nullptr;
        ChannelId channel{0};
        std::string channelName;
        double lastS = -1e300;
        std::shared_ptr<std::atomic<bool>> busy = std::make_shared<std::atomic<bool>>(false);
    };
    InstrumentRegistry& registry_;
    core::JobSystem& jobs_;
    core::EventBus& bus_;
    std::vector<Entry> entries_;
    std::vector<std::future<void>> pending_;
};

} // namespace qlab::instr
