// Spec 03 §3 — the whole command line of `quantumxlab` (see App.hpp). `src/main.cpp` is two lines
// over this, so the dispatcher itself is testable.
#include "App/App.hpp"
#include "Core/Log.hpp"
#include "Core/Paths.hpp"
#include <cstdlib>
#include <iostream>

namespace qlab::app {

int main(int argc, const char* const* argv) {
    const Result<Options> options = parseOptions(argc, argv);
    if (!options) {
        std::cerr << "quantumxlab: " << options.error().message << "\n";
        return 2;
    }
    switch (options->mode) {
    case Mode::Help:
        std::cout << usageText();
        return 0;
    case Mode::Version:
        std::cout << versionText() << "\n";
        return 0;
    default:
        break;
    }
    // Spec 03 §6: `--assets <dir>` is the first of the resolution order; `core::assetDir()` reads
    // `QXL_ASSETS`, so the override is exported before anything asks for an asset.
    if (!options->assets.empty()) {
#if defined(_WIN32)
        _putenv_s("QXL_ASSETS", options->assets.string().c_str());
#else
        setenv("QXL_ASSETS", options->assets.string().c_str(), 1);
#endif
    }
    // A headless mode keeps the console clean: only warnings and errors reach it.
    core::logInit(options->headless() ? core::LogLevel::Warn : core::LogLevel::Info);
    if (!std::filesystem::exists(core::assetDir())) {
        std::cerr << "quantumxlab: no asset directory at " << core::assetDir().string()
                  << " (use --assets <dir>)\n";
        return 2;
    }

    switch (options->mode) {
    case Mode::Run:
        return runHeadless(*options, std::cout, std::cerr);
    case Mode::SelfTest: {
        const Result<SelfTestReport> report = runSelfTest(*options, std::cout);
        if (!report) {
            std::cerr << "quantumxlab: " << report.error().format() << "\n";
            return 1;
        }
        for (const std::string& note : report->notes)
            std::cout << "note: " << note << "\n";
        std::cout << report->screenshots.size() << " screenshots, " << report->examples.size()
                  << " examples\n";
        return report->ok() ? 0 : 1;
    }
    case Mode::Gui:
        return runGui(*options);
    case Mode::Help:
    case Mode::Version:
        break;
    }
    return 0;
}

} // namespace qlab::app
