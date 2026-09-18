// Spec 03 §3/§5 — `quantumxlab --selftest <out_dir>`: build the laboratory, render every workspace
// and every camera bookmark to PNG, then run the shipped examples and assert their results.
//
// The example oracle is the Born distribution, not the sampled one: the run is ideal, so the exact
// probabilities the state vector carries must match `<name>.expected.json` to its own tolerance
// (spec 25 §2), and the sampled counts must agree with them inside five binomial sigmas.
//
// An expectation file holds `statevector`, `counts`, `tolerance`, `unitary_digest`, `shots`, `seed`
// and the optional `backend`, `partial`, `register` and `note`. `counts` is a map from a bitstring
// to its probability; `"register": "<name>"` reads those bitstrings on one classical register
// instead of on the whole classical memory (spec 15 §4), which is what a protocol example wants:
// teleportation's oracle is the one bit of `out`, not the three bits `out, m[1], m[0]` that a
// result's key spans. `"tolerance": "statistical"` with no `counts` map marks an oracle that is a
// fit or a reconstruction, and the checker skips it.
#include "App/Application.hpp"
#include "App/Headless.hpp"
#include "App/RunProgram.hpp"
#include "Core/Json.hpp"
#include "Core/JobSystem.hpp"
#include "Core/Log.hpp"
#include "Core/Paths.hpp"
#include "Data/Histogram.hpp"
#include "Hardware/Hardware.hpp"
#include "Lang/Lang.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <ostream>
#include <string>
#include <tuple>
#include <vector>

namespace qlab::app {
namespace {

constexpr double kSigmas = 5.0;   // spec 25 §1: a statistical assertion is a 5σ band

std::filesystem::path expectedFor(const std::filesystem::path& qasm) {
    std::filesystem::path p = qasm;
    p.replace_extension();
    return p.string() + ".expected.json";
}

std::string safeName(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) out.push_back(c == ' ' || c == '/' || c == '\\' ? '_' : static_cast<char>(std::tolower(c)));
    return out;
}

// ---------------------------------------------------------------- which device runs an example
// The shipped devices ordered by data-qubit count (spec 09 §4), read once from `device.json` alone:
// only the qubit count is wanted here, and the calibration of the 127-qubit lattice is not free.
struct ShippedDevice {
    std::string id;
    std::uint32_t dataQubits = 0;
};

const std::vector<ShippedDevice>& shippedDevices() {
    static const std::vector<ShippedDevice> kAll = [] {
        std::vector<ShippedDevice> all;
        for (const std::string& id : hw::shippedDeviceIds()) {
            const auto device = hw::loadDeviceJson(hw::deviceRoot() / id / "device.json");
            if (device) all.push_back({id, static_cast<std::uint32_t>(device->dataQubitCount())});
        }
        std::sort(all.begin(), all.end(), [](const ShippedDevice& a, const ShippedDevice& b) {
            return std::tie(a.dataQubits, a.id) < std::tie(b.dataQubits, b.id);
        });
        return all;
    }();
    return kAll;
}

// Spec 15 §1: a program's `pragma qlab.device` names the device it was written for, and
// `Session::compile` refuses to compile it for any other. Without one the smallest shipped device
// that holds the program runs it, so a 12-qubit program lands on the 27-qubit lattice and not on
// the 127-qubit one. A program larger than everything that ships runs on the largest, so that the
// failure names the real ceiling rather than the default device's.
std::string deviceFor(const lang::Program& program) {
    if (program.pragmas.device) return *program.pragmas.device;
    const std::vector<ShippedDevice>& devices = shippedDevices();
    for (const ShippedDevice& d : devices)
        if (d.dataQubits >= program.qubitCount) return d.id;
    return devices.empty() ? std::string("sc_fixed_5") : devices.back().id;
}

// ---------------------------------------------------------------- which bits a label addresses
// A result's key spans EVERY classical bit, most significant first (spec 15 §4), so an expectation
// that names one register is a marginal over that register's bits: `shift` is the register's first
// flat bit and `width` its size. Without `register` the window is the whole classical memory.
struct BitWindow {
    std::uint32_t shift = 0;
    std::size_t width = 0;
    std::uint64_t mask() const { return width >= 64 ? ~0ull : (1ull << width) - 1ull; }
    std::uint64_t valueOf(std::uint64_t key) const { return (key >> shift) & mask(); }
};

// P(label) read on the window: exactly, from the Born distribution over the classical bits.
double windowProbability(const std::vector<double>& exact, BitWindow window, std::uint64_t value) {
    double p = 0.0;
    for (std::uint64_t x = 0; x < exact.size(); ++x)
        if (window.valueOf(x) == value) p += exact[x];
    return p;
}

// The shots whose key carries `value` on the window.
std::uint64_t windowCount(const data::Histogram& counts, BitWindow window, std::uint64_t value) {
    std::uint64_t k = 0;
    for (const auto& [label, n] : counts.raw())
        if (window.valueOf(data::Histogram::indexFromLabel(label)) == value) k += n;
    return k;
}

std::string registerList(const runtime::ClassicalLayout& layout) {
    std::string out;
    for (const runtime::RegisterInfo& r : layout.registers)
        out += std::format("{}{}[{}]", out.empty() ? "" : ", ", r.name, r.size);
    return out;
}

} // namespace

bool SelfTestReport::ok() const {
    // A skipped example (a statistical oracle with no fixed distribution) is not a failure; an
    // example that ran and disagreed with its expectation is.
    for (const ExampleCheck& e : examples)
        if (!e.skipped && !e.passed) return false;
    return !examples.empty();
}

ExampleCheck checkExample(const std::filesystem::path& qasm, const Options& options) {
    ExampleCheck check;
    check.name = qasm.stem().string();     // `device` stays empty until the program picks one
    const auto text = core::readTextFile(expectedFor(qasm));
    if (!text) {
        check.detail = std::format("no expectation file: {}", expectedFor(qasm).string());
        return check;
    }
    core::Json expected;
    try {
        expected = core::Json::parse(*text);
    } catch (const std::exception& e) {
        check.detail = std::format("expectation file is not JSON: {}", e.what());
        return check;
    }
    const bool partial = expected.is_object() && expected["partial"].is_boolean() && expected["partial"].get<bool>();
    // `"tolerance": "statistical"` marks an example whose oracle is a fit or a reconstruction, not
    // a fixed distribution; `nlohmann::value()` would throw on the string, and an exception here
    // aborted the whole self-test after the fourth example (DEVELOPMENT.md: nothing throws across
    // a module boundary).
    const double tolerance =
        expected.is_object() && expected["tolerance"].is_number() ? expected["tolerance"].get<double>() : 1e-6;
    if (!expected.is_object() || !expected["counts"].is_object() || expected["counts"].empty()) {
        check.skipped = true;
        check.detail = expected.is_object() && expected["note"].is_string()
                           ? expected["note"].get<std::string>()
                           : "no fixed distribution to compare (statistical oracle)";
        return check;
    }

    // Spec 15 §1 — the program's own `pragma qlab.device`, else the smallest device that holds it:
    // one default device for the whole corpus turned every program wider than five qubits into a
    // failure that said nothing about the program (Shor, Bernstein–Vazirani, Simon, Grover-6).
    const auto source = core::readTextFile(qasm);
    if (!source) {
        check.detail = source.error().format();
        return check;
    }
    const lang::Program program = lang::analyzeProgram(*source, qasm.filename().string());
    if (!program.ok()) {
        check.detail = program.errors().front().format();
        return check;
    }
    check.device = deviceFor(program);

    Options run = options;
    run.device = check.device;
    run.ideal = true;                      // the oracle is the ideal distribution (spec 25 §2)
    run.shotsGiven = true;
    run.shots = expected["shots"].is_number_unsigned() ? expected["shots"].get<std::uint32_t>() : 1024u;
    run.seedGiven = true;
    run.seed = expected["seed"].is_number_unsigned() ? expected["seed"].get<std::uint64_t>() : 1ull;

    core::EventBus bus;
    runtime::Session session(&bus, &core::JobSystem::global());
    if (auto st = session.selectDevice(run.device); !st) {
        check.detail = st.error().message;
        return check;
    }
    const Result<ProgramRun> outcome = compileAndRun(session, *source, qasm, run);
    if (!outcome) {
        check.detail = outcome.error().format();
        return check;
    }
    check.ran = true;
    const runtime::RunResult& r = outcome->result;
    const auto& counts = expected["counts"];
    if (!counts.is_object() || counts.empty()) {
        check.detail = "the expectation file lists no counts";
        return check;
    }

    // The window the expectation's labels live in: one register when it names one, else the whole
    // classical memory.
    const bool named = expected["register"].is_string();
    const std::string oracle = named ? expected["register"].get<std::string>() : registerList(r.layout);
    BitWindow window{0, r.counts.nbits()};
    if (named) {
        const runtime::RegisterInfo* reg = r.layout.find(oracle);
        if (reg == nullptr) {
            check.detail = std::format("the expectation reads register '{}'; the program declares {}",
                                       oracle, registerList(r.layout));
            return check;
        }
        window = BitWindow{reg->first, reg->size};
    }

    double listed = 0.0;
    std::string worst;
    double worstDelta = -1.0;
    for (const auto& [label, probability] : counts.items()) {
        // A label of the wrong width can never match a key, and would silently read as P = 0.
        if (label.size() != window.width) {
            check.detail = std::format("the label '{}' does not span the oracle: {} bits against {} "
                                       "({}); name the register the expectation reads with "
                                       "\"register\", or write the whole key",
                                       label, label.size(), window.width, oracle);
            return check;
        }
        const double want = probability.get<double>();
        listed += want;
        const std::uint64_t value = data::Histogram::indexFromLabel(label);
        const std::uint64_t k = windowCount(r.counts, window, value);
        const double sampled =
            r.counts.total() ? static_cast<double>(k) / static_cast<double>(r.counts.total()) : 0.0;
        // The Born distribution is the oracle where the run holds it; a per-shot run (feed-forward,
        // reset, routed measurements) has none, and the sampled counts carry a 5σ band instead.
        const double got = r.exact ? windowProbability(*r.exact, window, value) : sampled;
        const double sigma5 = kSigmas * data::binomialSigma(k, r.counts.total());
        const double band = r.exact ? tolerance : std::max(tolerance, sigma5);
        if (const double delta = std::abs(got - want); delta > worstDelta) {
            worstDelta = delta;
            worst = std::format("{}: want {:.6f}, got {:.6f} (tolerance {:.3g})", label, want, got, band);
        }
        if (std::abs(got - want) > band) {
            check.detail = worst;
            return check;
        }
        // The sampled outcome must also sit inside five sigmas of the ideal probability.
        const double sigma = std::sqrt(std::max(want * (1.0 - want), 1e-12) /
                                       std::max<double>(1, static_cast<double>(r.counts.total())));
        if (std::abs(sampled - want) > kSigmas * sigma + tolerance) {
            check.detail = std::format("{}: sampled {:.4f} is more than 5σ from {:.4f}", label, sampled, want);
            return check;
        }
    }
    if (!partial && std::abs(listed - 1.0) > 1e-6) {
        check.detail = std::format("the expectation lists {:.6f} of the distribution but is not marked partial", listed);
        return check;
    }
    check.passed = true;
    // Always the worst deviation, never a bare count: "3 outcomes matched" reads the same whether
    // the checker compared three probabilities or three empty strings.
    check.detail = std::format("{} outcome{} matched, worst {}", counts.size(),
                               counts.size() == 1 ? "" : "s", worst);
    return check;
}

Result<SelfTestReport> runSelfTest(const Options& options, std::ostream& out) {
    SelfTestReport report;
    std::error_code ec;
    std::filesystem::create_directories(options.outDir, ec);
    if (ec) return fail(ErrorCode::Io, std::format("--selftest: cannot create {}", options.outDir.string()));

    // ---- examples (no GL needed)
    // Every example that ships an expectation is checked, not a chosen few: a wrong expectation is
    // exactly the kind of error a fixed shortlist hides (one did — see SPEC_DEVIATIONS).
    const std::filesystem::path examples = core::assetDir() / "Programs" / "Examples";
    std::vector<std::filesystem::path> withExpectation;
    if (std::filesystem::is_directory(examples, ec)) {
        for (const auto& e : std::filesystem::recursive_directory_iterator(examples)) {
            if (!e.is_regular_file() || e.path().extension() != ".qasm") continue;
            std::filesystem::path expected = e.path();
            expected.replace_extension(".expected.json");
            if (std::filesystem::exists(expected, ec)) withExpectation.push_back(e.path());
        }
    }
    std::sort(withExpectation.begin(), withExpectation.end());
    for (const std::filesystem::path& qasm : withExpectation) {
        ExampleCheck check = checkExample(qasm, options);
        const char* verdict = check.skipped ? "skip" : (check.passed ? "ok  " : "FAIL");
        out << std::format("example {:<26} {}  {:<19} {}\n", check.name, verdict,
                           check.device.empty() ? "-" : check.device, check.detail);
        report.examples.push_back(std::move(check));
    }

    // ---- renders
    auto app = Application::create(options, false);
    if (!app) {
        report.notes.push_back(std::format("no GL context: {}", app.error().message));
        out << "renders skipped: " << app.error().message << "\n";
        return report;
    }
    Application& a = **app;
    report.renderedScenes = true;

    // Run the Bell example inside the application, so that the captures show the state views, the
    // histogram and the lab overlays with real data rather than their placeholders.
    if (auto text = core::readTextFile(examples / "Basics" / "bell.qasm")) {
        a.setProgramSource(std::move(*text));
        a.requestRun();
        for (int i = 0; i < 600 && a.model().result() == nullptr; ++i) a.frame(1.0 / 60.0);
        if (a.model().result() == nullptr) report.notes.emplace_back("the in-application Bell run did not finish");
        // The reductions the state views asked for are computed on the job system; give them a few
        // frames to arrive so the tiles are not captured with their "stale" badge.
        for (int i = 0; i < 12; ++i) {
            a.frame(1.0 / 60.0);
            a.model().waitReductions();
        }
    }

    const auto shot = [&](const std::filesystem::path& file) {
        report.screenshots.push_back(file);
        out << "wrote " << file.string() << "\n";
    };

    // Spec 19 §2: every workspace, laid out by its own DockBuilder script.
    for (std::size_t w = 0; w < ui::kWorkspaceCount; ++w) {
        const auto workspace = static_cast<ui::Workspace>(w);
        a.showWorkspace(workspace, 4);
        const std::filesystem::path file =
            options.outDir / std::format("workspace_{}.png", ui::workspaceName(workspace));
        if (auto st = a.captureWindow(file); st) shot(file);
        else report.notes.push_back(st.error().format());
    }

    // Spec 17 §7.10: the guided tour's narration card over the laboratory, at the mixing-chamber
    // stop (the card must sit at the edge of the picture, not over the machine).
    a.showWorkspace(ui::Workspace::Lab, 2);
    if (lab::Tour* tour = a.model().tour(); tour != nullptr && tour->size() > 10) {
        tour->play();
        tour->seek(10);
        for (int i = 0; i < 80; ++i) a.frame(1.0 / 60.0);   // the 0.9 s flight and a moment of dwell
        const std::filesystem::path file = options.outDir / "workspace_tour.png";
        if (auto st = a.captureWindow(file); st) shot(file);
        else report.notes.push_back(st.error().format());
        tour->stop();
    }

    // Spec 17 §7: every camera bookmark of the layout, rendered from the laboratory viewport.
    a.showWorkspace(ui::Workspace::Lab, 2);
    if (lab::Interaction* ui = a.model().interaction(); ui != nullptr) {
        const core::Json view = ui->saveViewState();
        const std::vector<lab::LayoutBookmark> marks = ui->bookmarks();
        const std::size_t layoutMarks = a.model().scene() != nullptr ? a.model().scene()->layout().bookmarks.size() : 0;
        const lab::LayoutSpec* layout = a.model().scene() != nullptr ? &a.model().scene()->layout() : nullptr;
        for (std::size_t k = 0; k < marks.size() && k < std::max<std::size_t>(layoutMarks, 1); ++k) {
            ui->loadViewState(view);               // a chip bookmark hides layers; undo that first
            if (!ui->applyBookmark(marks[k].name, a.camera(), 0.0)) continue;
            // Spec 17 §7.5/§7.6: a bookmark whose camera sits inside the fridge frame is an
            // interior view — the vacuum can would fill it, so the cans go and X-ray fades the
            // shields to 12 %.
            if (layout != nullptr && layout->hasFridge()) {
                const glm::dvec3 d = marks[k].view.position - layout->fridgePosition_m;
                const bool interior =
                    std::abs(d.x) < 0.5 * layout->frameWidth_m && std::abs(d.z) < 0.5 * layout->frameDepth_m;
                ui->setCansVisible(!interior);
                ui->setXray(interior);
            }
            a.frame(1.0 / 60.0);
            const std::filesystem::path file = options.outDir / std::format("bookmark_{}.png", safeName(marks[k].name));
            if (auto st = a.captureViewport(file, 1280, 800); st) shot(file);
            else report.notes.push_back(st.error().format());
        }
        ui->loadViewState(view);
    }
    return report;
}

} // namespace qlab::app
