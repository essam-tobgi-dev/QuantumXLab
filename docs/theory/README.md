# QuantumXLab — Theory Corpus

These documents are the physics and mathematics that power the laboratory. Every formula the
application displays, every model it integrates, and every estimate it reports is derived or
stated here with its assumptions. The application renders these documents in-app
(spec `20`) and links to them from components, equations, and diagnostics.

Conventions:

- Equations are LaTeX. Inline `$…$`, display `$$…$$`. Numbered equations use `\tag{n.m}` with
  the section number.
- $\hbar$ is written explicitly. Frequencies are ordinary frequencies $f$ in Hz unless written
  $\omega = 2\pi f$ in rad/s. Energies of circuit elements are quoted as $E/h$ in GHz.
- Qubit ordering is little-endian: $|q_{n-1}\ldots q_1 q_0\rangle$, index $\sum_k q_k 2^k$.
- The Pauli matrices are $X, Y, Z$; $\sigma^\pm = (X \mp iY)/2$ so that $\sigma^- |1\rangle = |0\rangle$
  (lowering toward the ground state $|0\rangle$).
- Every document ends with a *Where this is used* section mapping results to specs.
- Values labelled "typical" are ranges for the stated platform and year (2023–2025 published
  devices); they parametrise the `Model` fidelity class, never the `Exact` one.

| # | Document | Contents |
|---|----------|----------|
| T01 | [T01-quantum-mechanics-foundations.md](T01-quantum-mechanics-foundations.md) | Postulates, Dirac notation, qubits, Bloch sphere, tensor products, entanglement, measurement, density operators, partial trace, Schmidt decomposition, entropies |
| T02 | [T02-gates-and-circuits.md](T02-gates-and-circuits.md) | Pauli/Clifford/universal sets, rotations, Euler and U3 decompositions, two-qubit gates, KAK, controlled gates, Toffoli, gate identities, circuit–unitary correspondence |
| T03 | [T03-algorithms.md](T03-algorithms.md) | Deutsch–Jozsa, Bernstein–Vazirani, Simon, QFT, phase estimation, Shor, Grover, VQE, QAOA, teleportation, superdense coding; complexity and resource counts |
| T04 | [T04-open-quantum-systems.md](T04-open-quantum-systems.md) | Kraus maps, Lindblad equation, amplitude and phase damping, $T_1$/$T_2$/$T_2^*$, depolarizing, thermal channel, quantum trajectories, Ramsey and echo, $1/f$ noise |
| T05 | [T05-superconducting-qubits.md](T05-superconducting-qubits.md) | LC quantization, Josephson junction, transmon Hamiltonian, charge dispersion, flux tunability, circuit QED, dispersive readout, Purcell, couplers, ZZ, cross-resonance, DRAG |
| T06 | [T06-trapped-ions.md](T06-trapped-ions.md) | Paul trap, motional modes, hyperfine and optical qubits, Lamb–Dicke regime, sideband transitions, Mølmer–Sørensen gate, fluorescence readout, heating |
| T07 | [T07-control-electronics-and-signals.md](T07-control-electronics-and-signals.md) | Rotating frame and RWA, Rabi frequency vs drive amplitude, IQ modulation, pulse shapes, heterodyne readout, demodulation, SNR, noise temperature, quantum-limited amplification, attenuation and thermal photons |
| T08 | [T08-cryogenics.md](T08-cryogenics.md) | Dilution refrigerator principle, cooling power, stage temperatures, conductive and radiative heat loads, coaxial-line heat load integrals, cooldown dynamics, residual qubit thermal population |
| T09 | [T09-error-correction.md](T09-error-correction.md) | Stabilizer formalism, Gottesman–Knill, repetition/Shor/Steane codes, surface code, syndrome extraction, decoding, threshold and logical error scaling, magic states, resource estimation |
| T10 | [T10-benchmarking-and-tomography.md](T10-benchmarking-and-tomography.md) | Fidelity definitions, randomized benchmarking (standard, interleaved), state and process tomography, cross-entropy benchmarking, quantum volume, readout calibration |
| T11 | [T11-simulation-numerics.md](T11-simulation-numerics.md) | State-vector gate kernels, density-matrix evolution, stabilizer tableau updates, matrix exponential, Lindblad vectorization and integration, sampling, precision and complexity |
| T12 | [T12-runtime-and-resource-estimation.md](T12-runtime-and-resource-estimation.md) | Wall-time model for physical devices, fidelity estimation from calibration, classical simulation cost, algorithm scaling with QEC overhead |
