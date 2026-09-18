# Development Guide

Working rules for anyone (person or coding agent) implementing a module. The authoritative
design is in `specs/` (start with `specs/README.md` and `specs/02-architecture.md`); the
physics is in `docs/theory/`. This file covers only how to work in the tree.

## Build and test

```bash
cmake --preset debug && cmake --build --preset debug -j10     # whole tree
ctest --preset debug                                          # all tests
# one module, isolated build directory (use this while developing a module):
cmake -S . -B build/dev-<module> -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/dev-<module> --target test_<module> -j6 && ./build/dev-<module>/bin/test_<module>
```

Third-party dependencies are fetched once into `.deps/` and shared by every build directory.

## How modules are wired

`CMakeLists.txt` declares every module with `qxl_module(<Name> <Dir> DEPS …)`. Sources are
globbed from `src/<Dir>/**/*.cpp` into the static library `qxl_<name>`; tests are globbed from
`tests/<Dir>/*.cpp` into the Catch2 executable `test_<name>`. **Do not edit `CMakeLists.txt`
to add files** — drop them in the directory and re-run the configure step. A module with no
`.cpp` files yet is an `INTERFACE` target, so dependants still configure.

| Dir | Target | Namespace | | Dir | Target | Namespace |
|-----|--------|-----------|-|-----|--------|-----------|
| Core | qxl_core | `qlab::core` | | IR | qxl_ir | `qlab::ir` |
| Units | qxl_units | `qlab::units` | | Compiler | qxl_compiler | `qlab::compiler` |
| Numerics | qxl_numerics | `qlab::num` | | QEC | qxl_qec | `qlab::qec` |
| QSim | qxl_qsim | `qlab::qsim` | | Runtime | qxl_runtime | `qlab::runtime` |
| Data | qxl_data | `qlab::data` | | Instruments | qxl_instr | `qlab::instr` |
| Hardware | qxl_hardware | `qlab::hw` | | Graphics | qxl_gfx | `qlab::gfx` |
| Noise | qxl_noise | `qlab::noise` | | Lab | qxl_lab | `qlab::lab` |
| Pulse | qxl_pulse | `qlab::pulse` | | Viz | qxl_viz | `qlab::viz` |
| Cryo | qxl_cryo | `qlab::cryo` | | UI | qxl_ui | `qlab::ui` |
| Lang | qxl_lang | `qlab::lang` | | Report / App | qxl_report / qxl_app | `qlab::report` / `qlab::app` |

Each module has an umbrella header `src/<Dir>/<Dir>.hpp` (e.g. `Numerics/Numerics.hpp`). Read a
dependency's umbrella header and the headers it includes before using it; the headers are the
API reference.

## Code rules (spec 01 §1, 02 §1)

- C++23, no compiler extensions. Must compile **warning-clean** under
  `-Wall -Wextra -Wpedantic -Wshadow` with AppleClang 21.
- Recoverable failures return `qlab::Result<T>` (`std::expected<T, qlab::Error>`, see
  `src/Core/Error.hpp`; helpers `fail(code, msg)`, `QXL_TRY`, `QXL_TRY_ASSIGN`). Each module
  owns an error-code block (`ErrorCode::Noise_ + n`, …). No exception crosses a module boundary.
- Layers 0–3 (everything except Graphics, Lab, Viz, UI, Report, App) MUST NOT include OpenGL,
  GLFW, or ImGui headers and must test headless.
- `std::complex<double>` for amplitudes; never `float` for physics. RAII for every resource;
  no `new`/`delete`. Strong types from `src/Core/StrongType.hpp` (`QubitIndex`, `ComponentId`,
  `Picoseconds`).
- **Qubit order is little-endian**: qubit 0 is the least significant bit of a basis index.
  A two-qubit gate declared on `(q_a, q_b)` has its matrix in the basis `|q_b q_a>`
  (`num::embed`: `targets[0]` is the least-significant index of the gate matrix).
- Fidelity is the squared convention. Depolarizing probability from an average error rate `r`
  is `p = d r / (d - 1)`. The qubit Hamiltonian is `-(1/2) hbar omega Z`, so `|0>` is the
  ground state at the north pole of the Bloch sphere.
- Randomness only through `core::Random` (seeded xoshiro256**, `stream(i)` per shot). Never
  `std::random_device`, never wall-clock seeds.
- JSON files use the envelope `{"qxl": {"kind", "schema", "app", "created"}, "data": {…}}`
  via `core::JsonEnvelope`. Loaders tolerate unknown fields and name the missing field path
  in the error.
- Every displayed quantity carries a fidelity class (`data::FidelityClass`: Exact, Numerical,
  Statistical, Model, Illustrative).
- Comments state intent and cite the spec or theory section (`// spec 08 §4.1`, `// T04 (4.5)`).
  No filler prose.

## Working method

- Write files in small steps: **one file of at most ~250 lines per write**, and compile after
  every two or three files. Split large components across several translation units.
- Tests are physics oracles, not smoke tests: assert closed-form results with stated
  tolerances (spec 25). If a test fails, find out whether the model or the expectation is
  wrong; never loosen an assertion just to make it pass.
- Stay inside the directories your task names. Shared assets live under `Assets/`
  (`Devices/<id>/{device,calibration,pulses,wiring}.json`, `Lab/Components/<id>/component.json`,
  `Lab/Layouts/<id>/`, `Theory/{equations,gates,assumptions}.json`, `QEC/`, `Analysis/`,
  `Programs/{Examples,Calibration,Conformance}/`, `Lang/{diagnostics,theme,strings.en}.json`).
  Device assets are generated by `python3 tools/gencal.py` (seeded, deterministic).
- Any place the implementation must diverge from a spec goes into `SPEC_DEVIATIONS.md` with
  the reason. If the spec itself is wrong, fix the spec and say so.

## Verified baseline (2026-09-18)

The whole tree builds and passes as one project, verified the way CI runs it:

```bash
cmake --preset debug && cmake --build --preset debug -j10 && ctest --preset debug
# 100% tests passed out of 781          (480 s)
# 1 240 954 assertions across 781 test cases
python3 tools/check_includes.py   # 0 layering violations (spec 02 §1)
python3 tools/check_xrefs.py      # 0 unresolved spec/theory references
python3 tools/gen_assets.py       # assets regenerate identically
./build/debug/bin/quantumxlab --selftest out/   # 18 ok, 12 statistical skips, 0 failures, 9 renders to look at
```

Run `ctest`, not the test binaries alone: a test whose name the runner cannot pass through as an
argument (a leading `--`, or a `;`, which splits the generated CMake list) is silently skipped by
`ctest` while the binary still reports it green. Four cases were broken that way.

| Module | Assertions | Cases | | Module | Assertions | Cases |
|---|---|---|-|---|---|---|
| core | 128 | 8 | | noise | 334 689 | 43 |
| units | 296 | 9 | | pulse | 8 176 | 40 |
| numerics | 1 023 | 25 | | cryo | 1 493 | 12 |
| data | 586 | 21 | | compiler | 394 342 | 64 |
| lang | 1 096 | 15 | | qec | 212 111 | 52 |
| ir | 2 021 | 46 | | runtime | 1 654 | 47 |
| qsim | 44 189 | 57 | | instr | 21 634 | 39 |
| hardware | 2 935 | 37 | | lab | 159 506 | 54 |
| gfx | 13 468 | 24 | | viz | 14 272 | 58 |
| ui | 6 688 | 68 | | report | 20 156 | 37 |
| app | 491 | 25 | | **total** | **1 240 954** | **781** |
