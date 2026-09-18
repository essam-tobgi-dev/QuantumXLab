#include "Instruments/Live.hpp"
#include <algorithm>
#include <chrono>
#include <format>
#include <map>

namespace qlab::instr {

LiveRunner::LiveRunner(InstrumentRegistry& registry, core::JobSystem& jobs, core::EventBus& bus)
    : registry_(registry), jobs_(jobs), bus_(bus) {}

LiveRunner::~LiveRunner() { waitIdle(); }

Result<void> LiveRunner::enable(const InstrumentId& id, std::string_view channel, bool on) {
    IInstrument* instrument = registry_.find(id);
    if (!instrument) return fail(err::UnknownInstrument, "no instrument " + id.toString());
    const auto ch = findChannel(*instrument, channel);
    if (!ch) return fail(err::UnknownChannel, std::format("{}: no channel '{}'", id.toString(), channel));
    const auto it = std::find_if(entries_.begin(), entries_.end(),
                                 [&](const Entry& e) { return e.instrument == instrument && e.channel == *ch; });
    if (!on) {
        if (it != entries_.end()) entries_.erase(it);
        return {};
    }
    if (it != entries_.end()) return {};
    Entry e;
    e.instrument = instrument;
    e.channel = *ch;
    e.channelName = std::string(channel);
    for (auto const& other : entries_) // one acquisition at a time per instrument: share its busy flag
        if (other.instrument == instrument) e.busy = other.busy;
    entries_.push_back(std::move(e));
    return {};
}

bool LiveRunner::enabled(const InstrumentId& id, std::string_view channel) const {
    return std::any_of(entries_.begin(), entries_.end(),
                       [&](const Entry& e) { return e.instrument->id() == id && e.channelName == channel; });
}

std::size_t LiveRunner::tick(double labTimeS) {
    // Finished futures are dropped so the list stays short.
    std::erase_if(pending_, [](std::future<void>& f) { return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready; });
    std::map<IInstrument*, std::vector<Entry*>> due;
    for (auto& e : entries_) {
        const double rate = settingNumber(e.instrument->get("refresh_hz")).value_or(10.0);
        if (e.instrument->state() == State::Off || e.busy->load()) continue;
        if (labTimeS - e.lastS + 1e-12 < 1.0 / rate) continue;
        due[e.instrument].push_back(&e);
    }
    std::size_t submitted = 0;
    for (auto& [instrument, channels] : due) {
        std::vector<std::pair<ChannelId, std::string>> work;
        for (Entry* e : channels) {
            e->lastS = labTimeS;
            work.push_back({e->channel, e->channelName});
        }
        auto busy = channels.front()->busy;
        busy->store(true);
        submitted += work.size();
        IInstrument* target = instrument;
        core::EventBus* bus = &bus_;
        pending_.push_back(jobs_.submit(
            [target, work = std::move(work), busy, bus](std::stop_token stop) {
                for (auto const& [channel, name] : work) {
                    if (stop.stop_requested()) break;
                    // A trigger source that is still set re-arms the instrument for this acquisition.
                    const SettingValue trigger = target->get("trigger");
                    if (const auto* s = std::get_if<std::string>(&trigger); s && *s != "free_run" && target->state() == State::Idle)
                        (void)target->execute(Command::of(Command::Kind::Arm));
                    auto trace = target->acquire(channel);
                    if (trace) bus->post(TraceReady{std::make_shared<const Trace>(std::move(*trace))});
                    else bus->post(AcquireFailed{target->id(), name, trace.error()});
                }
                busy->store(false);
            },
            core::JobPriority::Interactive));
    }
    return submitted;
}

void LiveRunner::waitIdle() {
    for (auto& f : pending_)
        if (f.valid()) f.wait();
    pending_.clear();
}

std::size_t LiveRunner::inFlight() const {
    std::size_t n = 0;
    for (auto const& e : entries_) n += e.busy->load() ? 1u : 0u;
    return n;
}

} // namespace qlab::instr
