# QuantumXLab

A desktop quantum-computing research laboratory in C++23 and OpenGL.

QuantumXLab combines an OpenQASM 3 / OpenPulse compiler, a numerically exact quantum simulator
with calibration-derived noise and pulse-level Lindblad dynamics, a resource and wall-time
estimator for physical hardware, and a 3D laboratory — dilution refrigerator, wiring,
control rack, instruments, processor chip — in which every component is selectable,
inspectable, and driven by the simulation. Every instrument in the rack produces the trace a
physical instrument would produce from the modelled signals.

## What it does

- **Write and compile.** OpenQASM 3 with OpenPulse `cal`/`defcal` blocks and `pragma qlab.*`
  directives, compiled onto a chosen virtual processor: decomposition to the native gate set,
  optimization, layout, SABRE routing, scheduling, and pulse lowering, with an equivalence
  checker that proves the compiled circuit matches the source.
- **Simulate.** Five backends — state vector, density matrix, stabilizer, Monte-Carlo
  trajectories, and a pulse-level Lindblad integrator that evolves the actual control pulses on
  a multi-level transmon or ion Hamiltonian. Noise is built from the device's calibration file.
- **Estimate.** Wall time and fidelity on the physical machine, with every assumption stated,
  plus surface-code resource estimates for a target logical error rate.
- **Explore.** A 3D lab of 2200 nodes — the dilution refrigerator as a Bluefors-class
  chandelier (gold-plated plates with bolt circles and chamfers, support posts, pulse-tube
  stages, still, heat exchangers, mixing chamber, flanged cans), every coaxial line, clamp,
  attenuator, amplifier and the chip down to individual qubits and junctions — where every part
  can be hovered for its explanation card, clicked, inspected, and read live off the running
  simulation. Rendered with image-based lighting, ambient occlusion, soft shadows and bloom.
- **Learn.** A narrated guided tour (31 stops for the superconducting lab, 16 for the ion trap)
  flies through the machine in signal order — racks, fridge, stage by stage to the chip and the
  qubits, back up the readout chain — explaining each part's purpose and physics with the live
  numbers and a link to the theory document.
- **Measure.** Working models of the signal generators, arbitrary waveform generator, digitizer,
  vector network analyzer, spectrum analyzer, oscilloscope and thermometry, plus simulator-only
  probes that are labelled as such and hidden in "physical lab" mode.

## Status

**Complete: 1 240 954 assertions across 781 test cases, all green under `ctest`**, built as one
tree on macOS (Apple silicon, OpenGL 4.1). `quantumxlab --selftest` renders every workspace and
checks all 30 shipped example programs against their expectations.

| Layer | Modules |
|-------|---------|
| Foundation | Core, Units, Numerics, Data |
| Simulation | QSim (5 backends), Noise, Hardware, Pulse, Cryo |
| Language | Lang (OpenQASM 3 + OpenPulse), IR, Compiler, QEC |
| Execution | Runtime, Instruments |
| Presentation | Graphics (GL 4.1), Lab (3D scene), Viz (17 state views), UI, Report |
| Application | App — the composition root and the `quantumxlab` executable |

Every module is in place; `quantumxlab --selftest` renders each workspace and checks the shipped
examples end to end. See `specs/26-roadmap.md` for what each phase delivered.

The tests are physics oracles, not smoke tests: gate decompositions are checked against their
unitaries, the surface-code decoder's threshold is measured (0.56 % circuit-level against
Fowler's 0.57 %), the Mølmer–Sørensen gate is integrated from the ion Hamiltonian and must
produce a Bell state and return the motional mode to its ground state, and the estimator's
worked examples must reproduce the numbers printed in the specification.

## Build

Requirements: CMake ≥ 3.28, Ninja, a C++23 compiler (AppleClang 21 / Clang 17+ / GCC 13+).
Dependencies are fetched and pinned automatically on first configure.

```bash
cmake --preset debug
cmake --build --preset debug -j10
ctest --preset debug            # or run build/debug/bin/test_<module> directly
```

For a fast, optimized binary use the release preset:

```bash
cmake --preset release && cmake --build --preset release -j10
```

## Run

```bash
./build/debug/bin/quantumxlab                      # the laboratory
./build/debug/bin/quantumxlab my_experiment.qxlab  # …opened on a project
```

The window opens on the **Lab** workspace: the 3D laboratory in the centre, the component tree on
the left, the Inspector and the Fridge dashboard on the right, the instrument rack below.
In the 3D laboratory, drag to orbit around the point under the cursor, right-drag (or
Shift-drag) to pan, scroll to zoom toward the cursor, Alt-drag to look around, `W A S D Q E` to
fly; hover any part for its explanation card, click to inspect it, double-click to fly to it,
`T` to open its theory; right-click a part for its menu (open or close the cans, fly to, theory,
cutaway, X-ray). The `?` chip in the viewport toolbar lists the controls.
`Ctrl+1/2/3` switch to the **Lab**, **Program** and **Analysis** workspaces; `F5` runs the program
in the editor, `F6` compiles it, `F10`/`F11` step the playhead by a gate or a shot. The
**Physical lab** toggle in the top bar hides everything a physical machine could not show you.
**View ▸ Text size** scales the interface between 0.8× and 1.6×; the display's own DPI only decides
how finely the type is rasterised.

Headless modes, for scripting and for CI:

```bash
# compile and run a program, print counts, expectation values and the hardware estimate
./build/debug/bin/quantumxlab --run Assets/Programs/Examples/Basics/bell.qasm \
    --device sc_fixed_5 --shots 4096 --seed 1

# the same as one `run_result` JSON document (spec 23 §6), Estimate included
./build/debug/bin/quantumxlab --run Assets/Programs/Examples/Search/grover_3.qasm --json

# build the lab, render every workspace and every camera bookmark to PNG, then run the
# shipped examples and check them against their expectation files
./build/debug/bin/quantumxlab --selftest out/
```

| Option | Meaning |
|--------|---------|
| `--run <program.qasm>` | headless: parse → compile → run → report |
| `--device <id>` | `sc_fixed_5`, `sc_heavyhex_27`, `sc_heavyhex_127`, `sc_tunable_grid_54`, `ion_chain_11`, `ion_chain_32` |
| `--shots N`, `--seed S` | shots and the seed every Statistical result is reproducible from |
| `--backend <kind>` | `auto`, `statevector`, `densitymatrix`, `stabilizer`, `lindblad` |
| `--ideal` | run without the calibration-derived noise model |
| `-O0`, `-O1`, `-O2` | compiler optimization level (default `-O1`) |
| `--json` | print the `run_result` document instead of the human-readable report |
| `--selftest <dir>` | headless render and example check; writes 10 PNGs (3 workspaces, the tour card, 6 bookmarks) and exits non-zero on a failure |
| `--layout <id>` | laboratory layout (`sc_lab_standard`, `ion_lab_11`) |
| `--physical-lab` | start with the Physical-lab toggle on |
| `--assets <dir>` | asset root; otherwise the compiled-in `Assets/` directory |
| `--version`, `--help` | |

Anything the command line does not pin is taken from the program's own `pragma qlab.*`
directives (shots, seed, backend, noise, optimization level), so a program carries its own
reproducible run configuration.

## Reading order

1. [`specs/README.md`](specs/README.md) — the engineering specification (27 documents).
2. [`docs/theory/README.md`](docs/theory/README.md) — the physics and mathematics behind every
   model, with derivations, in LaTeX (12 documents).
3. [`DEVELOPMENT.md`](DEVELOPMENT.md) — build system, module map, and the conventions every
   contributor follows.
4. [`SPEC_DEVIATIONS.md`](SPEC_DEVIATIONS.md) — every place the implementation diverges from the
   specification, and every specification error found and corrected while building it.

## Relationship to NewtoniumXLab

NewtoniumXLab is the sibling project for general physics experiments. QuantumXLab reuses its
engineering conventions (spec structure, fidelity classes, `std::expected` error handling, units
system, LaTeX rendering pipeline, oracle-test policy) but shares no code; its audience, depth,
and simulator are different.

## License

MIT.
