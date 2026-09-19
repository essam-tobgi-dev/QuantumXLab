// Spec 15 §1 — the session: device and backend selection, loaded programs, asynchronous compiles
// on the job system, and the events the UI subscribes to.
#include "Runtime/SessionState.hpp"
#include <chrono>
#include <format>

namespace qlab::runtime {

Session::Session(core::EventBus* bus, core::JobSystem* jobs) : bus_(bus), jobs_(jobs) {}

Session::~Session() {
    // Every worker holds a raw pointer into `compiles_`/`runs_`: stop them and join before the maps
    // go.
    std::vector<std::future<void>*> pending;
    {
        std::lock_guard lk(mu_);
        for (auto& [key, c] : compiles_) {
            c->stop.request_stop();
            if (c->future.valid())
                pending.push_back(&c->future);
        }
        for (auto& [key, r] : runs_) {
            r->stop.request_stop();
            if (r->future.valid())
                pending.push_back(&r->future);
        }
    }
    for (std::future<void>* f : pending)
        f->wait();
}

// ---------------------------------------------------------------- device and backend
Status Session::selectDevice(std::string_view deviceId) {
    QXL_TRY_ASSIGN(hw::LoadedDevice loaded, hw::loadShippedDevice(deviceId));
    setDevice(std::move(loaded));
    return {};
}

void Session::setDevice(hw::LoadedDevice loaded) {
    device_ = std::move(loaded);
    if (bus_)
        bus_->post(DeviceSelected{device_->device.id, device_->calibration.timestamp});
}

void Session::selectBackend(BackendChoice choice) {
    backend_ = choice;
    if (bus_)
        bus_->post(BackendSelected{choice});
}

// ---------------------------------------------------------------- programs
Result<ProgramId> Session::loadProgram(std::string source, std::filesystem::path origin) {
    auto e = std::make_unique<ProgramEntry>();
    e->program = lang::analyzeProgram(source, origin.filename().string());
    if (!e->program.ok()) {
        std::vector<Error> errors = e->program.errors();
        Error err =
            errors.empty() ? Error(ErrorCode::Parse, "the program has errors") : errors.front();
        for (std::size_t i = 1; i < errors.size(); ++i)
            err.notes.push_back(errors[i].message);
        return std::unexpected(std::move(err));
    }
    e->hash = compiler::programHash(source);
    e->source = std::move(source);
    e->origin = std::move(origin);
    std::lock_guard lk(mu_);
    const auto id = static_cast<std::uint32_t>(programs_.size());
    const std::string originText = e->origin.string();
    programs_.push_back(std::move(e));
    if (bus_)
        bus_->post(ProgramLoaded{ProgramId{id}, originText});
    return ProgramId{id};
}

Session::ProgramEntry* Session::entry(ProgramId id) const {
    std::lock_guard lk(mu_);
    return id.get() < programs_.size() ? programs_[id.get()].get() : nullptr;
}

const lang::Program* Session::program(ProgramId id) const {
    const ProgramEntry* e = entry(id);
    return e ? &e->program : nullptr;
}

std::string_view Session::source(ProgramId id) const {
    const ProgramEntry* e = entry(id);
    return e ? std::string_view(e->source) : std::string_view{};
}

// ---------------------------------------------------------------- compile (async, cancellable)
Session::CompileState* Session::findCompile(CompileHandle h) const {
    std::lock_guard lk(mu_);
    auto it = compiles_.find(h.get());
    return it == compiles_.end() ? nullptr : it->second.get();
}

Result<CompileHandle> Session::compile(ProgramId id, compiler::CompileOptions options) {
    ProgramEntry* prog = entry(id);
    if (!prog)
        return fail(err::UnknownHandle, "no such program");
    if (!device_)
        return fail(err::NoDevice, "select a device before compiling");
    // The program's `pragma qlab.device` names the device it was written for (spec 15 §1).
    if (prog->program.pragmas.device && *prog->program.pragmas.device != device_->device.id)
        return fail(err::NoDevice,
                    std::format("the program targets device '{}', the session holds '{}'",
                                *prog->program.pragmas.device, device_->device.id));
    auto state = std::make_unique<CompileState>();
    CompileState* raw = state.get();
    {
        std::lock_guard lk(mu_);
        raw->handle = CompileHandle{nextCompile_++};
        raw->program = id;
        raw->options = std::move(options);
        compiles_.emplace(raw->handle.get(), std::move(state));
    }
    if (bus_)
        bus_->post(CompileStarted{id, raw->handle});
    const hw::Device& dev = device_->device;
    const hw::Calibration& cal = device_->calibration;
    auto body = [raw, prog, &dev, &cal, bus = bus_](std::stop_token worker) {
        std::stop_callback link(std::move(worker), [raw] { raw->stop.request_stop(); });
        const auto t0 = std::chrono::steady_clock::now();
        Result<compiler::CompiledProgram> out =
            compiler::compile(prog->program, dev, cal, raw->options, raw->stop.get_token());
        std::uint32_t errors = 0, warnings = 0;
        if (out) {
            out->programHash = prog->hash;
            std::vector<lang::Diagnostic> all = prog->program.diagnostics;
            all.insert(all.end(), out->diagnostics.begin(), out->diagnostics.end());
            out->diagnostics = all;
            raw->diagnostics = std::move(all);
            raw->output = std::move(*out);
            raw->ok = true;
        } else {
            raw->error = out.error();
            ++errors;
        }
        for (const lang::Diagnostic& d : raw->diagnostics)
            (d.isError() ? errors : warnings) += 1;
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0);
        raw->finished.store(true, std::memory_order_release);
        if (bus)
            bus->post(CompileFinished{raw->program, raw->handle, raw->ok, errors, warnings, ms});
    };
    if (jobs_)
        raw->future = jobs_->submit(std::move(body), core::JobPriority::Interactive);
    else
        body(std::stop_token{});
    return raw->handle;
}

bool Session::compileDone(CompileHandle h) const {
    const CompileState* s = findCompile(h);
    return s && s->finished.load(std::memory_order_acquire);
}

Status Session::waitCompile(CompileHandle h) {
    CompileState* s = findCompile(h);
    if (!s)
        return fail(err::UnknownHandle, "no such compile");
    if (s->future.valid())
        s->future.wait();
    if (!s->ok)
        return std::unexpected(s->error);
    return {};
}

void Session::cancelCompile(CompileHandle h) {
    if (CompileState* s = findCompile(h))
        s->stop.request_stop();
}

const compiler::CompiledProgram* Session::compiled(CompileHandle h) const {
    const CompileState* s = findCompile(h);
    if (!s || !s->finished.load(std::memory_order_acquire) || !s->output)
        return nullptr;
    return &*s->output;
}

std::vector<lang::Diagnostic> Session::compileDiagnostics(CompileHandle h) const {
    const CompileState* s = findCompile(h);
    return s && s->finished.load(std::memory_order_acquire) ? s->diagnostics
                                                            : std::vector<lang::Diagnostic>{};
}

std::vector<RunRecord> Session::history() const {
    std::lock_guard lk(mu_);
    return history_;
}

} // namespace qlab::runtime
