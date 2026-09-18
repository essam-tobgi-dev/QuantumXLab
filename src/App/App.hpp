#pragma once
// Umbrella header for qlab::app (spec 02 §5–§6) — the composition root and the `quantumxlab`
// executable.
//
//   Options       the command line of spec 03 §3
//   LabModel      every subsystem bound together, headless: the session, the cryogenic stack, the
//                 instrument rack, the laboratory scene, the state views and the live bindings
//   LiveState     the one record the `lab::BindingRegistry` providers read (spec 17 §5)
//   ProjectHost   new / open / save / save-as `.qxlab`, autosave and crash recovery (spec 23 §2)
//   Application   window, renderer, ImGui, the main loop and the command routing (spec 02 §6)
//   runHeadless   `--run`; runSelfTest `--selftest`
//   app::main     the whole executable; `src/main.cpp` is two lines over it
#include "App/Application.hpp"
#include "App/Headless.hpp"
#include "App/Live.hpp"
#include "App/Model.hpp"
#include "App/Options.hpp"
#include "App/ProjectHost.hpp"
#include "App/RunProgram.hpp"

namespace qlab::app {

// Parses the command line, resolves the asset root and dispatches. Returns the process exit code.
int main(int argc, const char* const* argv);

} // namespace qlab::app
