#include "Instruments/InstrumentBase.hpp"
#include <cmath>
#include <format>

namespace qlab::instr {
namespace {
std::uint64_t fnv1a(std::string_view s) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 0x100000001b3ull;
    }
    return h;
}
} // namespace

double SettingValues::real(std::string_view key) const {
    auto it = values_.find(key);
    return it == values_.end() ? 0.0 : settingNumber(it->second).value_or(0.0);
}
std::int64_t SettingValues::integer(std::string_view key) const {
    return std::llround(real(key));
}
bool SettingValues::flag(std::string_view key) const {
    auto it = values_.find(key);
    if (it == values_.end())
        return false;
    if (auto* b = std::get_if<bool>(&it->second))
        return *b;
    return false;
}
std::string SettingValues::text(std::string_view key) const {
    auto it = values_.find(key);
    if (it == values_.end())
        return {};
    if (auto* s = std::get_if<std::string>(&it->second))
        return *s;
    return {};
}

InstrumentBase::InstrumentBase(InstrumentId id, SettingSchema schema)
    : id_(std::move(id)), schema_(std::move(schema)) {
    if (schema_.instrument.empty())
        schema_.instrument = id_.kind;
    for (auto const& s : schema_.settings)
        values_[s.key] = s.defaultValue;
}

void InstrumentBase::setChannels(std::vector<ChannelDesc> channels) {
    channels_ = std::move(channels);
    for (std::size_t i = 0; i < channels_.size(); ++i)
        channels_[i].id = ChannelId{static_cast<std::uint32_t>(i)};
}

SettingValues InstrumentBase::snapshot() const {
    std::lock_guard lk(mu_);
    return SettingValues(values_);
}
std::shared_ptr<const RunView> InstrumentBase::runView() const {
    return bindings_.inputs ? bindings_.inputs->run() : nullptr;
}
std::shared_ptr<const Environment> InstrumentBase::environment() const {
    return bindings_.inputs ? bindings_.inputs->environment() : nullptr;
}

std::string InstrumentBase::conflict(const SettingValues&) const {
    return {};
}
bool InstrumentBase::triggerSourceSet(const SettingValues& values) const {
    return values.has("trigger") && values.text("trigger") != "free_run";
}
void InstrumentBase::settingChanged(std::string_view) {}

void InstrumentBase::bind(const Bindings& bindings) {
    bindings_ = bindings;
}

void InstrumentBase::moveTo(State to) const {
    const State from = state_.exchange(to);
    if (from != to && bindings_.bus)
        bindings_.bus->post(StateChanged{id_, from, to});
}

void InstrumentBase::revalidate() {
    const std::string message = conflict(snapshot());
    bool runtime = false;
    {
        std::lock_guard lk(mu_);
        if (!message.empty()) {
            fault_ = message;
            runtimeFault_ = false;
        } else if (!runtimeFault_)
            fault_.clear();
        runtime = runtimeFault_;
    }
    const State s = state_.load();
    if (s == State::Off || s == State::Acquiring)
        return; // Off keeps the message; acquire() re-checks
    if (!message.empty()) {
        if (s != State::Fault && bindings_.bus)
            bindings_.bus->post(InstrumentFault{id_, message});
        moveTo(State::Fault);
    } else if (s == State::Fault && !runtime) {
        moveTo(State::Idle);
    }
}

Result<void> InstrumentBase::store(std::string_view key, SettingValue value, bool internal) {
    QXL_TRY_ASSIGN(Coerced c, schema_.coerce(key, value, internal));
    std::optional<SettingReport> report;
    {
        std::lock_guard lk(mu_);
        auto it = values_.find(key);
        if (it != values_.end())
            it->second = c.value;
        else
            values_.emplace(std::string(key), c.value);
        runtimeFault_ = false; // a new configuration gets a new chance; acquire() detects it again
        if (c.clamped) {
            report = SettingReport{id_,     std::string(key), std::move(value),
                                   c.value, c.message,        ++reportSeq_};
            reports_.push_back(*report);
            while (reports_.size() > kMaxReports)
                reports_.pop_front();
        }
    }
    if (report && bindings_.bus)
        bindings_.bus->post(SettingClamped{*report});
    settingChanged(key);
    revalidate();
    if (key == "trigger") { // spec 12 §1: Idle → Armed when a trigger source is set
        const bool armed = triggerSourceSet(snapshot());
        State s = state_.load();
        if (armed && s == State::Idle)
            moveTo(State::Armed);
        else if (!armed && s == State::Armed)
            moveTo(State::Idle);
    }
    return {};
}

Result<void> InstrumentBase::set(std::string_view key, SettingValue value) {
    return store(key, std::move(value), false);
}
Result<void> InstrumentBase::storeInternal(std::string_view key, SettingValue value) {
    return store(key, std::move(value), true);
}

SettingValue InstrumentBase::get(std::string_view key) const {
    std::lock_guard lk(mu_);
    auto it = values_.find(key);
    return it == values_.end() ? SettingValue{} : it->second;
}

std::string InstrumentBase::faultMessage() const {
    std::lock_guard lk(mu_);
    return state_.load() == State::Fault ? fault_ : std::string{};
}

std::vector<SettingReport> InstrumentBase::reports() const {
    std::lock_guard lk(mu_);
    return {reports_.begin(), reports_.end()};
}

std::unexpected<Error> InstrumentBase::raiseFault(ErrorCode code, std::string message) const {
    {
        std::lock_guard lk(mu_);
        fault_ = message;
        runtimeFault_ = true;
    }
    if (bindings_.bus)
        bindings_.bus->post(InstrumentFault{id_, message});
    moveTo(State::Fault);
    return std::unexpected(Error(code, id_.toString() + ": " + message));
}

Trace InstrumentBase::makeTrace(const ChannelDesc& channel, const AcquireContext& ctx) const {
    Trace t;
    t.instrument = id_.toString();
    t.channel = channel.name;
    t.xUnit = channel.xUnit;
    t.yUnit = channel.yUnit;
    t.cls = channel.cls;
    t.simulatorOnly = channel.simulatorOnly;
    t.t = ctx.stamp;
    return t;
}

Trace InstrumentBase::scalarTrace(const ChannelDesc& channel, const AcquireContext& ctx,
                                  double value) const {
    Trace t = makeTrace(channel, ctx);
    t.x = {ctx.stamp.labTimeS};
    t.y = {value};
    return t;
}

Result<Trace> InstrumentBase::acquire(ChannelId channel) {
    if (channel.get() >= channels_.size())
        return fail(err::UnknownChannel,
                    std::format("{}: no channel {}", id_.toString(), channel.get()));
    State s = state_.load();
    for (;;) {
        if (s == State::Off)
            return fail(err::PoweredOff, id_.toString() + " is switched off");
        if (s == State::Fault)
            return fail(err::Faulted, id_.toString() + " fault: " + faultMessage());
        if (s == State::Acquiring)
            return fail(err::Busy, id_.toString() + " is already acquiring");
        if (state_.compare_exchange_weak(s, State::Acquiring))
            break;
    }
    if (bindings_.bus)
        bindings_.bus->post(StateChanged{id_, s, State::Acquiring});

    AcquireContext ctx;
    ctx.settings = snapshot();
    ctx.run = runView();
    ctx.env = environment();
    ctx.stamp.labTimeS = ctx.env ? ctx.env->labTimeS : 0.0;
    ctx.stamp.runTimeS = ctx.run ? ctx.run->playheadS : 0.0;
    {
        std::lock_guard lk(mu_);
        ctx.stamp.sequence = ++acquireSeq_;
    }
    const std::uint64_t seed =
        ctx.run ? ctx.run->seed : (ctx.env ? ctx.env->seed : 0x5EEDC0FFEE17ull);
    ctx.rng = core::Random(seed ^ fnv1a(id_.toString())).stream(ctx.stamp.sequence);

    Result<Trace> result = doAcquire(channels_[channel.get()], ctx);
    if (state_.load() ==
        State::Acquiring) { // no runtime fault: Acquiring → Idle, or Fault on a conflict
        state_.store(State::Idle);
        if (bindings_.bus)
            bindings_.bus->post(StateChanged{id_, State::Acquiring, State::Idle});
        revalidate();
    }
    return result;
}

Result<void> InstrumentBase::execute(const Command& command) {
    using K = Command::Kind;
    const State s = state_.load();
    if (s == State::Acquiring && command.kind != K::Set)
        return fail(err::Busy, id_.toString() + " is acquiring");
    switch (command.kind) {
    case K::Set:
        return set(command.key, command.value);
    case K::PowerOn:
        if (s == State::Off) {
            moveTo(State::Idle);
            revalidate();
        }
        return {};
    case K::PowerOff:
        moveTo(State::Off);
        return {};
    case K::Arm:
        if (s == State::Armed)
            return {};
        if (s != State::Idle)
            return fail(err::BadState,
                        std::format("{} cannot arm from {}", id_.toString(), stateName(s)));
        moveTo(State::Armed);
        return {};
    case K::Disarm:
        if (s == State::Armed)
            moveTo(State::Idle);
        return {};
    case K::ClearFault: {
        {
            std::lock_guard lk(mu_);
            runtimeFault_ = false;
        }
        revalidate();
        if (state_.load() == State::Fault)
            return fail(err::Faulted, id_.toString() + " fault persists: " + faultMessage());
        return {};
    }
    case K::Preset: {
        {
            std::lock_guard lk(mu_);
            for (auto const& spec : schema_.settings)
                values_[spec.key] = spec.defaultValue;
            reports_.clear();
            runtimeFault_ = false;
        }
        for (auto const& spec : schema_.settings)
            settingChanged(spec.key);
        if (s == State::Armed)
            moveTo(State::Idle);
        revalidate();
        return {};
    }
    }
    return {};
}

std::optional<double> InstrumentBase::query(std::string_view path) const {
    return settingNumber(get(path));
}

core::Json InstrumentBase::saveSettings() const {
    std::lock_guard lk(mu_);
    core::Json out = core::Json::object();
    for (auto const& spec : schema_.settings)
        if (!spec.readOnly)
            if (auto it = values_.find(spec.key); it != values_.end())
                out[spec.key] = settingValueToJson(it->second);
    return out;
}

Result<void> InstrumentBase::loadSettings(const core::Json& values) {
    if (!values.is_object())
        return fail(err::BadSchema, id_.toString() + ": settings must be a JSON object");
    for (auto const& [key, v] : values.items()) {
        const SettingSpec* spec = schema_.find(key);
        if (!spec || spec->readOnly)
            continue; // unknown fields are tolerated (DEVELOPMENT.md)
        SettingValue value = settingValueFromJson(v);
        if (spec->type == SettingType::Real)
            if (auto n = settingNumber(value); n && !std::holds_alternative<bool>(value))
                value = *n;
        QXL_TRY(set(key, std::move(value)));
    }
    return {};
}

std::optional<ChannelId> findChannel(const IInstrument& instrument, std::string_view name) {
    for (auto const& c : instrument.channels())
        if (c.name == name)
            return c.id;
    return std::nullopt;
}

Result<Trace> acquire(IInstrument& instrument, std::string_view channelName) {
    auto ch = findChannel(instrument, channelName);
    if (!ch)
        return fail(err::UnknownChannel,
                    std::format("{}: no channel '{}'", instrument.id().toString(), channelName));
    return instrument.acquire(*ch);
}

} // namespace qlab::instr
