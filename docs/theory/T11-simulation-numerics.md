# T11 — Simulation Numerics

This document specifies the algorithms behind the four simulation backends: state-vector,
density-matrix, stabilizer, and Lindblad (pulse-level). It fixes index conventions, kernel
structure, integrators, error tolerances, and the cost model used to pick a backend and to
predict run time.

Notation: $n$ qubits, $N = 2^n$ amplitudes, $G$ gates. Machine epsilon
$\epsilon_{\rm mach} = 2^{-52}\approx 2.2\times10^{-16}$ (IEEE double). Amplitudes are always
`std::complex<double>`.

## 1. State-vector representation

The state $|\psi\rangle = \sum_{i=0}^{N-1}\psi_i|i\rangle$ is stored as a contiguous array
$\psi[0..N-1]$. Little-endian: bit $t$ of the index $i$ is the value of qubit $t$,
$$
i = \sum_{t=0}^{n-1} q_t\,2^t, \qquad q_t = (i \gg t)\,\&\,1 . \tag{1.1}
$$
Consequences: $|q_1 q_0\rangle = |10\rangle$ is index 2, not 1; the tensor product $A\otimes B$
with $A$ on qubit 1 and $B$ on qubit 0 has matrix elements $(A\otimes B)_{ij} =
A_{i_1 j_1}B_{i_0 j_0}$, i.e. the matrix of the full operator is $A\otimes B$ in the usual
Kronecker sense with the *higher* qubit on the left. Printing a basis state writes $q_{n-1}$
first.

Memory: $16N$ bytes.

| $n$ | $N$ | State vector | Density matrix $16N^2$ |
|-----|-----|-------------|----------------------|
| 8 | 256 | 4 KiB | 1 MiB |
| 10 | 1 024 | 16 KiB | 16 MiB |
| 12 | 4 096 | 64 KiB | 256 MiB |
| 13 | 8 192 | 128 KiB | 1 GiB |
| 14 | 16 384 | 256 KiB | 4 GiB |
| 16 | 65 536 | 1 MiB | — |
| 20 | $1.05\times10^6$ | 16 MiB | — |
| 24 | $1.68\times10^7$ | 256 MiB | — |
| 26 | $6.7\times10^7$ | 1 GiB | — |
| 28 | $2.7\times10^8$ | 4 GiB | — |
| 30 | $1.07\times10^9$ | 16 GiB | — |
| 32 | $4.3\times10^9$ | 64 GiB | — |

## 2. Gate kernels

### 2.1 Single-qubit gate

A gate $U = \begin{pmatrix}u_{00}&u_{01}\\u_{10}&u_{11}\end{pmatrix}$ on target qubit $t$ acts on
every pair of amplitudes whose indices differ only in bit $t$:
$$
\begin{pmatrix}\psi'_{i_0}\\ \psi'_{i_1}\end{pmatrix} = U\begin{pmatrix}\psi_{i_0}\\ \psi_{i_1}\end{pmatrix},
\qquad i_1 = i_0 \,|\, 2^t,\quad (i_0 \,\&\, 2^t) = 0. \tag{2.1}
$$
Enumerate $i_0$ by inserting a zero bit at position $t$ into a counter $k\in[0, N/2)$:

```
mask_lo = (1 << t) - 1
for k in 0 .. N/2 - 1:
    i0 = ((k & ~mask_lo) << 1) | (k & mask_lo)     # insert 0 at bit t
    i1 = i0 | (1 << t)
    a, b = psi[i0], psi[i1]
    psi[i0] = u00*a + u01*b
    psi[i1] = u10*a + u11*b
```

Cost: $N/2$ iterations, 8 complex multiply-adds each, $32N$ bytes of memory traffic (read
and write every amplitude once). On a 100 GB/s memory system the $n=28$ state (4 GiB)
takes $\approx 90$ ms per gate; $n=24$ (256 MiB) $\approx 5$ ms; below $n\approx 20$ the state
fits in cache and the kernel becomes compute-bound at $\approx 1$–$2$ ns per pair.

### 2.2 Two-qubit gate

Targets $t_1 > t_0$; each group of four amplitudes with the other bits fixed is multiplied by
the $4\times4$ matrix. Enumerate $k\in[0, N/4)$ and insert two zero bits:

```
for k in 0 .. N/4 - 1:
    i = insert_zero(insert_zero(k, t0), t1)          # low bit first, then high
    idx = [i, i | 1<<t0, i | 1<<t1, i | 1<<t0 | 1<<t1] # order: (q_t1 q_t0) = 00, 01, 10, 11
    v = psi[idx]; psi[idx] = U4 @ v
```

The index order in `idx` matches the gate matrix ordering in which the *first listed qubit
is the least significant* — for a gate declared `cx q[a], q[b]` (control $a$, target $b$) the
IR stores the matrix in the basis $|q_b q_a\rangle$ (spec `07 §2`). Cost: $N/4$ iterations, 16
complex multiply-adds each; memory traffic identical to the single-qubit case.

### 2.3 $k$-qubit generic kernel

For $k\le 5$ targets: enumerate $N/2^k$ groups by inserting $k$ zero bits (sorted target
positions), gather $2^k$ amplitudes, multiply by the $2^k\times2^k$ matrix, scatter. Beyond
$k=5$ the compiler decomposes.

### 2.4 Fast paths

- **Diagonal gates** ($R_z$, $S$, $T$, $CZ$, $CPhase$, `rzz`): $\psi_i \leftarrow d_{\,i\text{-bits}}\,\psi_i$;
  no pairing; half the memory traffic of the generic path. $R_z$ and $CZ$ with no other gate
  pending are additionally deferred and merged (§2.6).
- **Permutation gates** ($X$, `swap`, `cx`): pure index permutation, one read and one write
  per amplitude, no arithmetic.
- **Controlled gates**: apply the target kernel only to indices whose control bits are all 1.
  Enumerate with the control bits forced to 1 (insert-one instead of insert-zero), which
  halves the iteration count per control.
- **Global phase**: recorded, not applied.

### 2.5 Measurement

Probability that qubit $t$ reads 1:
$$
p_1 = \sum_{i:\,(i\gg t)\&1 = 1} |\psi_i|^2 . \tag{2.2}
$$
Collapse: draw $r\sim U(0,1)$, outcome $m = [r < p_1]$; zero the amplitudes with bit $t\neq m$
and scale the rest by $1/\sqrt{p_m}$. Cost: two passes. Multi-qubit measurement at the end of a
circuit uses the sampling path (§2.7) instead of sequential collapse.

### 2.6 Gate fusion

Consecutive single-qubit gates on the same qubit are multiplied into one $2\times2$ matrix
before the kernel runs. Any sequence of gates touching at most $k_{\max}$ distinct qubits
($k_{\max}=4$ by default, 5 when $n\ge 24$) is fused into a $2^{k}\times 2^{k}$ matrix, so a
typical circuit performs 3–6× fewer passes over memory. Diagonal gates fuse with anything on
overlapping qubits. Fusion is performed by the runtime on the scheduled circuit, not by the
compiler, and is invisible to the user.

### 2.7 Sampling without collapse

For $S$ shots of an $n$-qubit terminal measurement: compute $P_i = |\psi_i|^2$ and its prefix
sums $C_i = \sum_{j\le i}P_j$ in one pass, then for each shot draw $r$ and binary-search $C$
($O(S\log N)$). For $S > N/\log N$ use the alias method ($O(N)$ setup, $O(1)$ per draw). Both
paths are exact samplers; the result is bit-identical for a given seed across backends and
platforms.

### 2.8 Expectation values of Pauli strings

Write $P = i^{\,n_Y}\,X^{x}Z^{z}$ with $x$ the mask of qubits carrying $X$ or $Y$, $z$ the mask of
qubits carrying $Z$ or $Y$, and $n_Y$ the number of $Y$ factors (using $Y = iXZ$). Then
$P|i\rangle = i^{\,n_Y}(-1)^{\mathrm{popcount}(i\,\&\,z)}\,|i\oplus x\rangle$ and
$$
\langle P\rangle = \sum_i \overline{\psi_{i\oplus x}}\; i^{\,n_Y}\,(-1)^{\mathrm{popcount}(i\,\&\,z)}\;\psi_i , \tag{2.3}
$$
one pass, no matrix, real result up to rounding (the imaginary part is checked $< 10^{-12}$).
Sums of Pauli strings (Hamiltonians for VQE) are accumulated term by term with Kahan
compensation.

## 3. Parallelization

- Split the outer loop over $k$ into $T$ contiguous chunks (one per worker). Each chunk touches
  a disjoint index set, so no synchronization is needed inside a gate; a fork-join barrier
  separates gates.
- For targets $t < 6$ (pairs within the same cache line region), assign chunks of $\ge 4096$
  amplitudes to keep each worker streaming; for large $t$ the two halves of a pair are $2^t$
  apart and both streams are sequential — hardware prefetch handles both cases.
- Cache blocking: when the state exceeds L2, gates whose targets are all in the low
  $b = \log_2(\text{L2 bytes}/32)$ bits are grouped and applied block-by-block (one block of
  $2^b$ amplitudes stays resident across all gates in the group). Combined with fusion this
  is the main throughput lever at $n\ge 24$.
- SIMD: the pair update vectorizes over 2 (SSE/NEON) or 4 (AVX2) complex numbers by
  processing consecutive $k$; the compiler auto-vectorizes the kernel when the loop body is
  written with explicit real/imaginary arrays (structure-of-arrays for `psi` is NOT used;
  interleaved complex is retained because it halves the number of streams).

## 4. Density-matrix backend

$\rho$ is stored row-major, $N\times N$. A unitary $U$ on qubit set $Q$ acts as
$$
\rho \leftarrow U\rho U^\dagger , \tag{4.1}
$$
implemented as: (i) the state-vector kernel applied to every **column** treating the row
index as the amplitude index (this is $U\rho$); (ii) the kernel with $\overline{U}$ applied
to every **row** treating the column index as the amplitude index (this is $(\cdot)U^\dagger$,
since $(\rho U^\dagger)_{ij} = \sum_k \rho_{ik}\overline{U_{jk}}$). Both steps reuse the state-vector
kernel code with a stride parameter; cost $2N$ kernel applications of size $N$, i.e.
$O(N^2)$ per gate, memory traffic $64N^2$ bytes.

A Kraus channel $\{K_k\}$ is $\rho\leftarrow\sum_k K_k\rho K_k^\dagger$: accumulate into a
second buffer (memory $2\times16N^2$). Pauli channels use the diagonal/permutation fast paths
and need no second buffer.

Measurement of qubit $t$: $p_m = \mathrm{Tr}(\Pi_m\rho)$ from the diagonal; collapse
$\rho\leftarrow \Pi_m\rho\Pi_m/p_m$ zeroes rows and columns. Non-selective measurement
(dephasing) zeroes the off-diagonal blocks only.

Backend selection rule: density matrix is used whenever a noise model is attached and
$n\le 13$ (1 GiB); above that, Monte-Carlo trajectories on the state-vector backend (§7).

## 5. Superoperators and the Lindbladian

### 5.1 Vectorization

Column-stacking $\mathrm{vec}(\rho)$ (column $j$ of $\rho$ occupies entries $jN..jN+N-1$) gives
$$
\mathrm{vec}(A\rho B) = (B^{T}\otimes A)\,\mathrm{vec}(\rho). \tag{5.1}
$$
The application stores $\rho$ row-major, which equals column-major storage of $\rho^T$; the
implementation therefore uses $\mathrm{vec}(\rho^T)$ and the identity $\mathrm{vec}(A\rho B)^{T\text{-form}}
= (A\otimes B^T)\mathrm{vec}(\rho^T)$. All formulas below are stated for column stacking; the
code comments state the transposed form.

### 5.2 Lindblad equation and its superoperator

$$
\dot\rho = \mathcal L\rho = -\frac{i}{\hbar}[H,\rho] + \sum_k\left(L_k\rho L_k^\dagger - \tfrac12\{L_k^\dagger L_k,\rho\}\right) \tag{5.2}
$$
vectorizes to
$$
\mathcal L = -\frac{i}{\hbar}\left(I\otimes H - H^{T}\otimes I\right)
+ \sum_k\left[\overline{L_k}\otimes L_k - \tfrac12\, I\otimes L_k^\dagger L_k - \tfrac12\,(L_k^\dagger L_k)^{T}\otimes I\right]. \tag{5.3}
$$
$\mathcal L$ is an $N^2\times N^2$ matrix (for $N = 3^{n_q}$ levels of transmons it is
$9^{n_q}\times 9^{n_q}$: $n_q = 3$ → $729\times729$, $n_q = 5$ → $59\,049\times 59\,049$, too large to
store densely; the pulse-level backend never forms $\mathcal L$ for $n_q\ge 4$ and instead
applies (5.2) matrix-free with dense $H$ and $L_k$).

### 5.3 Time-dependent Hamiltonians

Under a pulse schedule $H(t) = H_0 + \sum_c s_c(t)\,H_c$ with control envelopes $s_c(t)$
sampled at the AWG rate (spec `10 §3`). The backend works in the rotating frame of $H_0$
(§6.4) and integrates the remaining slowly varying generator.

## 6. Time integration

### 6.1 Classical Runge–Kutta (RK4)

For $\dot y = f(t,y)$ with step $h$:
$$
k_1 = f(t,y),\ k_2 = f(t+\tfrac h2, y+\tfrac h2 k_1),\ k_3 = f(t+\tfrac h2, y+\tfrac h2 k_2),\ k_4 = f(t+h, y+hk_3),\quad
y_{+} = y + \tfrac h6(k_1+2k_2+2k_3+k_4). \tag{6.1}
$$
Local error $O(h^5)$, global $O(h^4)$. Four evaluations of $\mathcal L\rho$ per step. RK4 is not
trace- or positivity-preserving; the norm is monitored (§9). Used for the Lindblad backend
with a fixed step set by §6.5.

### 6.2 Dormand–Prince 5(4)

Embedded pair: seven stages, a fifth-order solution $y_5$ and a fourth-order $y_4$ from the
same stages (FSAL: the last stage of one step is the first of the next, so six new
evaluations per step). Error estimate $\mathrm{err} = \|y_5 - y_4\|_{\rm scaled}$ with
$\|\cdot\|_{\rm scaled}$ the RMS of components divided by $(\mathrm{atol} + \mathrm{rtol}\,|y|)$.
Step control:
$$
h_{\rm new} = h\,\min\!\left(5,\ \max\!\left(0.2,\ 0.9\,\mathrm{err}^{-1/5}\right)\right), \tag{6.2}
$$
accept the step if $\mathrm{err}\le 1$. Defaults: $\mathrm{rtol} = 10^{-8}$, $\mathrm{atol} =
10^{-10}$. The Butcher tableau is the standard DP5(4) (Dormand & Prince 1980); coefficients
are stored as exact rationals in the source. Used for the thermal model (spec `11`), for
Rabi/Ramsey scans with smooth envelopes, and as the reference integrator in tests.

### 6.3 Exponential midpoint for piecewise-constant generators

When the schedule is piecewise constant on AWG samples of length $\Delta t$ (typical
$\Delta t = 0.5$–$1$ ns), the exact propagator over one sample is
$$
\rho(t+\Delta t) = e^{\mathcal L(t_{\rm mid})\,\Delta t}\rho(t) \quad\text{or}\quad
|\psi(t+\Delta t)\rangle = e^{-iH(t_{\rm mid})\Delta t/\hbar}|\psi(t)\rangle \tag{6.3}
$$
(second-order accurate for slowly varying envelopes; exact for truly piecewise-constant
ones). The exponential is computed by §6.6 for $N\le 64$ (per-sample eigendecomposition of
the Hermitian $H$) or by a Krylov step for larger dimensions. Because $H(t_{\rm mid})$ changes
each sample, the cost is one expm per sample; with caching of exponentials for repeated
sample values (flat-top pulses) this is the fastest method for closed systems and the
default for the pulse-level backend when no dissipators are active.

### 6.4 Rotating frame

Write $H(t) = H_0 + V(t)$ with $H_0$ diagonal in the computational basis (qubit and resonator
frequencies). Define $\tilde\rho = e^{iH_0t/\hbar}\rho\, e^{-iH_0 t/\hbar}$. Then
$$
\dot{\tilde\rho} = -\frac{i}{\hbar}[\tilde V(t),\tilde\rho] + \tilde{\mathcal D}(\tilde\rho),\qquad
\tilde V(t) = e^{iH_0t/\hbar}V(t)e^{-iH_0t/\hbar}, \tag{6.4}
$$
whose matrix elements oscillate at the *differences* $\omega_i - \omega_j - \omega_d$ between
level frequencies and the drive frequency instead of at the absolute frequencies
($\sim 5$ GHz). Dissipators built from ladder operators of $H_0$ eigenstates are invariant
under the frame change. Step count drops from $\approx 20$ samples per 200 ps period to
$\approx 20$ samples per period of the largest *residual* frequency (detuning, anharmonicity
$|\alpha|/2\pi\approx 300$ MHz → 3.3 ns period → 0.17 ns steps for RK4; or exactly the AWG
sample for the exponential method).

### 6.5 Step-size selection

The fixed step for RK4 must resolve (i) the highest frequency present in the frame,
$f_{\max}$ = max over kept transitions of $|\omega_{ij} - \omega_d|/2\pi$ plus the envelope
bandwidth $B_{\rm env}\approx 1/\sigma$ for Gaussian pulses of width $\sigma$; (ii) the
accuracy target. RK4 with $h = 1/(20 f_{\max})$ gives a per-step phase error
$\approx (2\pi f_{\max}h)^5/120 \approx 2.5\times10^{-5}$ rad, i.e. $\approx 10^{-3}$ over a 40-step
pulse — insufficient for a $10^{-4}$ gate-error target. The backend therefore uses
$h = 1/(50 f_{\max})$ for RK4 (phase error $\approx 3\times10^{-7}$ per step) or the exponential
method, which has no such limit. The choice and the resulting $h$ are displayed with the run.

### 6.6 Matrix exponential

**Hermitian $H$ (closed system):** $e^{-iH\tau} = V e^{-i\Lambda\tau}V^\dagger$ with $H =
V\Lambda V^\dagger$ from a Hermitian eigensolver (Jacobi for $N\le 16$, tridiagonal QL for
larger). Cost $O(N^3)$ per distinct $H$.

**General $A$ (Lindbladian, or $-iH\tau$ without eigendecomposition):** scaling and squaring
with a degree-13 Padé approximant (Higham 2005):

1. Compute $\|A\|_1$; choose $s = \max(0, \lceil\log_2(\|A\|_1/\theta_{13})\rceil)$ with
   $\theta_{13} = 5.371920351148152$.
2. $A\leftarrow A/2^s$. Form $U, V$ from the even/odd parts of the Padé numerator using the
   coefficients $b_0..b_{13}$ of $r_{13}$ (six matrix multiplications).
3. Solve $(V - U)R = (V+U)$ for $R = r_{13}(A)$.
4. Square $s$ times: $e^A = R^{2^s}$.

Relative error $\le \epsilon_{\rm mach}$ in exact arithmetic for $\|A\|\le\theta_{13}$; the
lower-degree approximants ($m = 3,5,7,9$) with their own $\theta_m$ are used when
$\|A\|_1$ is small to save multiplications, as in Higham's algorithm.

**Closed forms** used for single-qubit rotations and tests:
$$
e^{-i\theta\,\hat n\cdot\vec\sigma/2} = \cos\tfrac\theta2\,I - i\sin\tfrac\theta2\,(\hat n\cdot\vec\sigma), \tag{6.5}
$$
$$
e^{-i\theta (Z\otimes Z)/2} = \cos\tfrac\theta2\,I - i\sin\tfrac\theta2\,Z\otimes Z, \tag{6.6}
$$
and, for any involution $P$ ($P^2 = I$), $e^{-i\theta P/2} = \cos\frac\theta2 I - i\sin\frac\theta2 P$.

**Krylov (Lanczos/Arnoldi):** for sparse or matrix-free $A$ of dimension $\gtrsim 10^3$,
build an $m$-dimensional Krylov basis $Q_m$ ($m = 20$–$30$) from $v$, form $H_m = Q_m^\dagger A Q_m$,
and approximate $e^{A\tau}v\approx Q_m e^{H_m\tau}e_1\|v\|$ with the small exponential by
Padé. Error estimate from the last Lanczos coefficient; the step $\tau$ is halved if it
exceeds the tolerance. Used by the Lindblad backend for $n_q\ge 4$ transmons.

## 7. Monte-Carlo wave function (quantum trajectories)

Equivalent to the Lindblad equation on average, at state-vector cost. With effective
non-Hermitian Hamiltonian $H_{\rm eff} = H - \frac{i\hbar}{2}\sum_k L_k^\dagger L_k$:

```
psi ← initial state ; r ← Uniform(0,1)
for each step of length h:
    psi ← exp(-i H_eff h / ħ) psi          # norm decays
    if ||psi||^2 < r:                       # a jump occurred in this step
        p_k ← h ||L_k psi||^2 for each k ; choose k with probability ∝ p_k
        psi ← L_k psi / ||L_k psi|| ; r ← Uniform(0,1)
    (jump time refined by bisection within the step when h ||L psi||^2 > 0.01)
at the end: renormalize psi ; measure / record observables
```

Averaging observables over $M$ trajectories estimates $\mathrm{Tr}(O\rho)$ with statistical
error $\sigma_O/\sqrt M$, where $\sigma_O\le\|O\|$; for probabilities $\sigma\le 1/(2\sqrt M)$,
so $M = 10^4$ gives $\pm 0.005$. Each trajectory uses its own random stream (§10). For
gate-level (non-pulse) noisy simulation the same idea reduces to sampling one Kraus operator
per gate with probability $\|K_k\psi\|^2$ — the *Pauli-sampling* path used above $n=13$.

## 8. Stabilizer backend

The tableau of T09 §3.1 is stored as $2n$ rows of $2n+1$ bits packed in 64-bit words
(column-packed variant: each of the $2n$ Pauli columns is a bit-vector over rows, which makes
the per-gate update a handful of word-wise XORs over $2n/64$ words). Costs: $O(n/64)$ words
per Clifford gate, $O(n^2/64)$ per measurement (rowsum loop). Non-Clifford gates are rejected
with a diagnostic naming the gate and the fallback backend. $10^4$ qubits × $10^6$ gates run
in seconds; the backend is used for QEC simulations (spec `16`) and for Clifford
conformance tests against the state vector (spec `25 §3`).

## 9. Precision policy

- Amplitudes and matrices: `double` complex throughout. `float` is never used for state
  data; the GPU path (spec `24 §5`) uses `double` where available and is otherwise disabled.
- Norm drift: each gate kernel contributes relative rounding error $\lesssim 4\epsilon_{\rm mach}$
  per amplitude; over $G$ gates the norm deviates by $\sim\sqrt G\,\epsilon_{\rm mach}$ (random
  walk). For $G = 10^6$: $\sim 2\times10^{-13}$. Policy: renormalize only when
  $|\,\|\psi\|^2 - 1| > 10^{-10}$ and log the event; a deviation $> 10^{-6}$ is an internal error.
- Gate matrices are checked unitary on construction: $\|U^\dagger U - I\|_{\max} < 10^{-12}$.
  Kraus sets are checked trace-preserving to $10^{-12}$.
- Density matrices: Hermiticity enforced by symmetrization $\rho\leftarrow(\rho+\rho^\dagger)/2$
  after every $10^3$ integrator steps; trace renormalized when $|\mathrm{Tr}\rho - 1| > 10^{-10}$;
  a negative eigenvalue below $-10^{-8}$ is flagged (integrator step too large).
- Integrators: RK4 global error is estimated by a step-halving check on the first 1 % of
  the schedule; DP5(4) reports its own estimate.
- Test tolerances: unitary equivalence up to phase $\|U_1 - e^{i\phi}U_2\|_{\max} < 10^{-10}$;
  probabilities $10^{-12}$ for exact paths; statistical tests at 5σ with the stated $N$.

## 10. Random numbers and reproducibility

- Generator: xoshiro256** (Blackman–Vigna), 256-bit state, period $2^{256}-1$, seeded from a
  64-bit run seed through SplitMix64.
- Stream discipline: one generator per (run seed, shot index) via jump-ahead
  ($2^{128}$ steps), one per trajectory, one per Monte-Carlo fault sample. A run with seed
  $s$ therefore produces identical shot memory regardless of thread count or shot ordering.
- Uniform doubles are formed from the top 53 bits ($2^{-53}$ resolution). Normal deviates
  by Marsaglia polar; exponential by inverse CDF.
- The seed is stored in every `RunResult` and every exported file; "Re-run with same seed"
  is a first-class command.

## 11. Cost model (used for backend selection and time prediction)

Per-gate time on a machine with memory bandwidth $B$ (bytes/s) and $T$ workers:
$$
t_{\rm gate}\approx\max\!\left(\frac{32\cdot 2^n}{B},\ \frac{2^n\cdot c_{\rm op}}{T}\right),\qquad
c_{\rm op}\approx 1.5\ \text{ns (1q)},\ 3\ \text{ns (2q)}, \tag{11.1}
$$
run time $\approx G_{\rm fused}\,t_{\rm gate} + t_{\rm sample}(S)$ with $G_{\rm fused}\approx G/4$
after fusion. Density matrix: replace $2^n$ by $2\cdot 4^n$. Stabilizer: $G\cdot n/64\cdot 1$ ns
plus measurements. Lindblad pulse-level: (number of AWG samples) × (expm or 4 matvecs of size
$N^2$).

| Backend | Limit in app | Reason |
|---------|-------------|--------|
| State vector | $n\le 28$ (32 GB machine), $n\le 30$ (64 GB) | memory $16\cdot2^n$ + one scratch buffer |
| Density matrix | $n\le 13$ | $2\times16\cdot 4^n$ = 2 GiB |
| Stabilizer | $n\le 20\,000$ | $O(n^2)$ bits and measurement time |
| Lindblad (3-level transmons) | $n_q\le 5$ | dimension $3^5 = 243$, $\rho$ has $59\,049$ entries, Krylov steps |
| Lindblad (2-level, gate-model noise) | $n\le 8$ | dense superoperator $4^8\times4^8$ avoided; matrix-free |

The runtime displays the prediction from (11.1) before a run and the measured time after,
and uses the ratio to recalibrate $B$ and $c_{\rm op}$ for the session.

## 12. Matrix product states (not in v1)

For circuits with limited entanglement (depth-limited, 1D-connected), an MPS with bond
dimension $\chi$ stores $n\cdot 2\chi^2$ amplitudes and applies a two-qubit gate on neighbours
in $O(\chi^3)$ via SVD truncation. Listed here because the cost model must recognise the
regime where it wins ($\chi\lesssim 2^{n/2}$ with $n > 30$); not implemented in v1.

## Where this is used

| Result | Spec |
|--------|------|
| Index convention (1.1), memory table | `07 §1`, `24 §1` |
| Kernels (§2), fusion, fast paths, sampling | `07 §2–4`, `24 §3` |
| Parallelization and cache blocking (§3) | `24 §3` |
| Density-matrix kernels, Kraus application (§4) | `07 §4`, `08 §2` |
| Vectorization and Lindbladian (5.1)–(5.3) | `06 §5`, `07 §6` |
| RK4, DP5(4), exponential midpoint, rotating frame, step selection (§6) | `06 §6`, `07 §6`, `10 §5`, `11 §6` |
| Padé expm, eigensolver, Krylov (§6.6) | `06 §3` |
| Quantum trajectories (§7) | `07 §7`, `08 §4` |
| Stabilizer packing (§8) | `07 §5` |
| Precision policy (§9) | `25 §2`, `06 §1` |
| RNG (§10) | `04 §6`, `15 §2` |
| Cost model (§11) | `15 §3`, `24 §2` |
