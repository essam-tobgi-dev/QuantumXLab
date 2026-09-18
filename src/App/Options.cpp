// Spec 03 §3 — command-line parsing (see Options.hpp).
#include "App/Options.hpp"
#include "Core/Version.hpp"
#include "Report/Types.hpp"
#include <charconv>
#include <format>

namespace qlab::app {
namespace {

std::unexpected<Error> badArg(std::string_view flag, std::string_view why) {
    return fail(err::BadArgument, std::format("{}: {}", flag, why));
}

// Accepts decimal digits only: an option value is never a computed expression.
template <class T> bool parseInteger(std::string_view text, T& out) {
    if (text.empty()) return false;
    const char* first = text.data();
    const char* last = text.data() + text.size();
    T value{};
    const auto [ptr, ec] = std::from_chars(first, last, value);
    if (ec != std::errc{} || ptr != last) return false;
    out = value;
    return true;
}

} // namespace

std::string defaultLayoutFor(std::string_view deviceId) {
    return deviceId.starts_with("ion_") ? "ion_lab_11" : "sc_lab_standard";
}

std::string versionText() { return std::format("quantumxlab {}", core::version()); }

std::string usageText() {
    return std::format(
        "{}\n"
        "\n"
        "Usage:\n"
        "  quantumxlab [project.qxlab]                       open the laboratory\n"
        "  quantumxlab --run <program.qasm> [options]        headless: compile, run, report\n"
        "  quantumxlab --selftest <out_dir>                  headless: render every workspace,\n"
        "                                                    run the shipped examples\n"
        "  quantumxlab --version | --help\n"
        "\n"
        "Run options:\n"
        "  --device <id>      device to compile and run for (default sc_fixed_5)\n"
        "  --shots N          shots (default 1024)\n"
        "  --seed S           seed of every Statistical result (default 1)\n"
        "  --backend <kind>   auto | statevector | densitymatrix | stabilizer | lindblad\n"
        "  --ideal            run without the calibration-derived noise model\n"
        "  -O0 | -O1 | -O2    compiler optimization level (default -O1)\n"
        "  --json             print the RunResult and the Estimate as one JSON document\n"
        "\n"
        "Everywhere:\n"
        "  --assets <dir>     asset root (default: the compiled-in Assets directory)\n"
        "  --layout <id>      laboratory layout (default: from the device technology)\n"
        "  --physical-lab     start with the Physical-lab toggle on (hides Simulator-only views)\n",
        versionText());
}

Result<Options> parseOptions(std::span<const std::string_view> args) {
    Options o;
    bool sawPositional = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view a = args[i];
        // A flag that takes a value: `--flag value` or `--flag=value`.
        const auto eq = a.find('=');
        const std::string_view name = eq == std::string_view::npos ? a : a.substr(0, eq);
        bool inlineValue = eq != std::string_view::npos;
        auto value = [&](std::string_view& out) -> Status {
            if (inlineValue) {
                out = a.substr(eq + 1);
                if (out.empty()) return badArg(name, "empty value");
                return {};
            }
            if (i + 1 >= args.size()) return badArg(name, "expects a value");
            out = args[++i];
            return {};
        };

        if (name == "--help" || name == "-h") {
            o.mode = Mode::Help;
            return o;
        }
        if (name == "--version" || name == "-v") {
            o.mode = Mode::Version;
            return o;
        }
        if (name == "--run") {
            std::string_view v;
            QXL_TRY(value(v));
            o.mode = Mode::Run;
            o.program = v;
        } else if (name == "--selftest") {
            std::string_view v;
            QXL_TRY(value(v));
            o.mode = Mode::SelfTest;
            o.outDir = v;
        } else if (name == "--device") {
            std::string_view v;
            QXL_TRY(value(v));
            o.device = v;
        } else if (name == "--layout") {
            std::string_view v;
            QXL_TRY(value(v));
            o.layout = v;
        } else if (name == "--assets") {
            std::string_view v;
            QXL_TRY(value(v));
            o.assets = v;
        } else if (name == "--shots") {
            std::string_view v;
            QXL_TRY(value(v));
            if (!parseInteger(v, o.shots) || o.shots == 0) return badArg(name, std::format("'{}' is not a shot count", v));
            o.shotsGiven = true;
        } else if (name == "--seed") {
            std::string_view v;
            QXL_TRY(value(v));
            if (!parseInteger(v, o.seed)) return badArg(name, std::format("'{}' is not a seed", v));
            o.seedGiven = true;
        } else if (name == "--backend") {
            std::string_view v;
            QXL_TRY(value(v));
            // `report::backendChoiceFrom` accepts every spelling a file or a pragma may carry.
            o.backend = report::backendChoiceFrom(v);
            if (o.backend == runtime::BackendChoice::Auto && v != "auto")
                return badArg(name, std::format("unknown backend '{}'", v));
            o.backendGiven = true;
        } else if (name == "--json") {
            o.json = true;
        } else if (name == "--ideal") {
            o.ideal = true;
        } else if (name == "--physical-lab") {
            o.physicalLab = true;
        } else if (name == "-O0" || name == "-O1" || name == "-O2") {
            o.optimize = name[2] - '0';
            o.optimizeGiven = true;
        } else if (name.starts_with("-")) {
            return badArg(name, "unknown option (try --help)");
        } else {
            if (sawPositional) return badArg(a, "only one project file may be given");
            sawPositional = true;
            o.project = a;
        }
    }
    if (o.mode == Mode::Run && o.program.empty()) return badArg("--run", "expects a program path");
    if (o.mode == Mode::SelfTest && o.outDir.empty()) return badArg("--selftest", "expects an output directory");
    if (o.layout.empty()) o.layout = defaultLayoutFor(o.device);
    return o;
}

Result<Options> parseOptions(int argc, const char* const* argv) {
    std::vector<std::string_view> args;
    args.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    return parseOptions(std::span<const std::string_view>(args));
}

} // namespace qlab::app
