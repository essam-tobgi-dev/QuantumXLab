#pragma once
// Spec 03 §3 — the headless modes of the executable: `--run` (parse → compile → run → report) and
// `--selftest` (render every workspace and bookmark, then run the shipped examples and check them).
// Neither needs a visible window; `--run` needs no GL context at all.
#include "App/Options.hpp"
#include <iosfwd>
#include <string>
#include <vector>

namespace qlab::app {

// Compiles and runs `options.program`, then writes either the `run_result` JSON document of
// spec 23 §6 (`--json`, which carries the Estimate of spec 15 §9) or a human-readable summary.
// Returns the process exit code: 0 on success, 1 on a compile or run failure.
int runHeadless(const Options& options, std::ostream& out, std::ostream& err);

// One checked example of `--selftest`.
struct ExampleCheck {
    std::string name;
    std::string device;       // the device the example ran on; empty when it was skipped
    bool ran = false;
    bool passed = false;
    // The expectation declares a statistical oracle (a fit or a tomographic reconstruction) rather
    // than a fixed distribution, so there is nothing for this checker to compare.
    bool skipped = false;
    std::string detail;       // the failing expectation, or the worst deviation when it passed
};

// Spec 25 §2 — runs `Assets/Programs/Examples/<category>/<name>.qasm` and compares its counts with
// the `<name>.expected.json` beside it (`counts` are probabilities; `tolerance` is on each of them;
// `partial` means the listed outcomes need not be the whole distribution; `register` names the one
// classical register the labels read, the whole classical memory being the default).
//
// The run is always ideal (spec 25 §2). The device is the program's own `pragma qlab.device`
// (spec 15 §1) when it has one, else the smallest shipped device whose data-qubit count holds the
// program — `options.device` does not decide it, since one default device cannot carry a corpus
// that runs from one qubit to twelve.
ExampleCheck checkExample(const std::filesystem::path& qasm, const Options& options);

struct SelfTestReport {
    std::vector<std::filesystem::path> screenshots;
    std::vector<ExampleCheck> examples;
    std::vector<std::string> notes;     // why a step was skipped (no GL context, missing asset)
    bool renderedScenes = false;
    bool ok() const;
};

// Spec 03 §5: checks every shipped example that carries an expectation, then builds the lab scene
// and renders each workspace and each camera bookmark to PNG under `options.outDir`. The examples
// come first because they need no GL context, so `--selftest` is still an oracle on a headless box.
Result<SelfTestReport> runSelfTest(const Options& options, std::ostream& out);

} // namespace qlab::app
