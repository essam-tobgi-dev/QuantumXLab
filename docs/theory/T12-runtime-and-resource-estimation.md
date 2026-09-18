# T12 — Runtime and Resource Estimation for Physical Hardware

This document defines the model behind every "on a real machine this would take…" figure
the application reports: wall time of a run, expected result fidelity, classical simulation
cost for comparison, scaling of algorithms on hardware, and the error-corrected regime.
Every estimate is fidelity class `Model` and is displayed with the assumption list of §9.

Notation: $N_{\rm shots}$ shots; $G_1, G_2$ counts of one- and two-qubit gates after
compilation; $t_{1}, t_{2}, t_{\rm ro}, t_{\rm reset}$ durations from the device calibration;
$\epsilon_g$ the average error rate of gate $g$ (T10 §1.3); $T_1, T_2$ per-qubit coherence
times.

## 1. Wall-time model

### 1.1 The formula

$$
T_{\rm run} = T_{\rm load} + N_{\rm shots}\,\big(T_{\rm reset} + T_{\rm circ} + T_{\rm ro} + T_{\rm gap}\big) + T_{\rm ff}, \tag{1.1}
$$

| Term | Meaning | How it is obtained |
|------|---------|--------------------|
| $T_{\rm load}$ | Fixed cost per job: compile on the control system, waveform upload, LO/AWG arming | Device file `control.load_time_ms` (spec `09 §2`): 50 ms (shipped transmon devices), 100 ms (shipped ion devices). Cloud services add 0.5–3 s of queue and job-assembly time that is excluded by definition |
| $T_{\rm reset}$ | Return all used qubits to $\lvert 0\rangle$ | §1.3 |
| $T_{\rm circ}$ | Critical path of the scheduled circuit up to the last gate before measurement | §1.2 |
| $T_{\rm ro}$ | Readout (all measured qubits read simultaneously where the device allows) | Calibration `readout_duration_ns` + device `timing.readout_ringdown_ns` ($\approx 4/\kappa$): 640 + 160 ns shipped transmon, 300 μs + 0 shipped ion |
| $T_{\rm gap}$ | Control-system dead time between shots (trigger re-arm, data transfer, repetition-period rounding) | Device file `control.rep_overhead_us`: 1.0 μs shipped transmon (typical 0.5–5 μs), 100 μs shipped ion (typical 50–200 μs) |
| $T_{\rm ff}$ | Extra time from mid-circuit measurement + feedforward: per feedforward point, readout + decision latency (typical 300–700 ns SC, 10–50 μs ions) × shots | Counted in $T_{\rm circ}$ for each shot; listed separately in the report |

### 1.2 Critical path

The compiler produces an ASAP schedule (spec `14 §7`): each operation $o$ gets a start time
$s_o = \max_{o'\prec o}(s_{o'} + t_{o'})$ over its predecessors in the dependency DAG (gates
sharing a qubit, barriers, classical dependencies). Then
$$
T_{\rm circ} = \max_o\,(s_o + t_o) \quad\text{over all non-measurement operations}. \tag{1.2}
$$
Parallel gates on disjoint qubits overlap; the critical path is the longest chain, not the
sum. Virtual $R_z$ gates have $t = 0$ (frame update, T07 §3). Delays declared in the program
count with their length. When measurements are in the middle of the circuit they are on the
path with duration $t_{\rm ro}$ (+ latency if a classical branch depends on them).

### 1.3 Reset

| Method | Duration | Availability |
|--------|----------|--------------|
| Passive (wait for relaxation) | $\max(k\,T_1^{\max}, t_{\rm rep})$ with $k = 5$ (residual excitation $e^{-5} = 0.7\%$) or $k=10$ ($4.5\times10^{-5}$); $k$ = `control.reset.passive_multiplier`, $t_{\rm rep}$ = `timing.repetition_delay_us` | All |
| Active, measurement-conditioned $X$ | $t_{\rm meas} + \tau_{\rm fb} + t_1$ = 640 + 200 + 32 ns = 872 ns on `sc_heavyhex_27` (`control.reset.policy: active`) | SC devices with feedforward |
| Active, unconditional (drive $\lvert 1\rangle\to\lvert 2\rangle\to$ resonator) | 0.3–1 μs | SC devices with the `reset.unconditional` flag |
| Optical pumping + recooling | `control.reset.cooling_time_ms` = 1.5 ms shipped (0.5–2 ms; Doppler + sideband cooling of the chain dominates) | Ion traps |

Passive reset makes $T_{\rm reset} = \max(5\times 150\ \mu{\rm s},\ 250\ \mu{\rm s}) = 750\ \mu$s
dominate every SC estimate on the shipped calibration; the report shows both and defaults to
the device's declared method.

### 1.4 Parameter table (typical, 2023–2025 devices)

| Quantity | Fixed-frequency transmon (CR) | Tunable-coupler transmon (CZ) | Trapped ion ($^{171}$Yb$^+$/$^{40}$Ca$^+$ chain) |
|----------|------|------|------|
| $t_1$ | 20–40 ns | 20–30 ns | 1–20 μs |
| $t_2$ | 200–500 ns (CR + echo) | 30–100 ns | 100–500 μs (MS) |
| $t_{\rm ro}$ | 0.3–1.5 μs | 0.3–1 μs | 0.1–1 ms |
| $T_1$ | 50–500 μs | 20–100 μs | $10^{3}$ s (hyperfine) |
| $T_2^{\rm echo}$ | 50–300 μs | 20–100 μs | 0.1–10 s |
| $\epsilon_1$ | $2$–$5\times10^{-4}$ | $1$–$5\times10^{-4}$ | $10^{-5}$–$10^{-4}$ |
| $\epsilon_2$ | $5\times10^{-3}$–$2\times10^{-2}$ | $3$–$8\times10^{-3}$ | $1$–$5\times10^{-3}$ |
| $\epsilon_{\rm ro}$ | 1–3 % | 0.5–2 % | 0.05–0.5 % |
| Connectivity | heavy-hex, degree ≤ 3 | square grid, degree 4 | all-to-all (chain ≤ 30) |

Sources are recorded per device file (spec `09 §4`); the ranges above are the regimes the
shipped nominals of the three device families sit in.

## 2. Worked examples

All examples use the shipped nominals of spec `09 §2` and `09 §4`. `sc_heavyhex_27`
(fixed-frequency, cross-resonance): $t_1 = 32$ ns (`sx`), $t_2 = 380$ ns (`cx`),
$t_{\rm ro} = 640 + 160$ ns $= 800$ ns (measurement + ring-down), active reset $872$ ns,
$T_{\rm gap} = 1\ \mu$s, $T_{\rm load} = 50$ ms, $\epsilon_1 = 3.5\times10^{-4}$,
$\epsilon_2 = 7.5\times10^{-3}$, $\epsilon_{\rm ro} = 2\%$ ($F_{\rm ro} = 0.98$), $T_1 = 150\ \mu$s,
$T_2^{\rm echo} = 95\ \mu$s. `ion_chain_11`: $t_1 = 10\ \mu$s, $t_2 = 200\ \mu$s (`ms`),
$t_{\rm ro} = 300\ \mu$s, cooling reset $1.5$ ms, $T_{\rm gap} = 100\ \mu$s, $T_{\rm load} = 100$ ms,
$\epsilon_1 = 10^{-4}$, $\epsilon_2 = 4\times10^{-3}$, $\epsilon_{\rm ro} = 0.3\%$, $T_2^{*} = 1$ s.
Spec `15 §6` carries the Bell example with the same inputs.

### 2.1 Bell state, 1024 shots

Compiled: `sx` on q0 (the `h` becomes $R_z\sqrt X R_z$, one pulse), `cx` q0→q1, measure both.

Transmon: $T_{\rm circ} = 32 + 380 = 412$ ns. Per shot $= 872 + 412 + 800 + 1000 = 3\,084$ ns.
$N_{\rm shots}\times = 3.158$ ms. $T_{\rm run} = 50 + 3.16 = 53.2$ ms — the load time dominates;
the report states "shots: 3.2 ms, load: 50 ms". With passive reset ($750\ \mu$s): per shot
$752.2\ \mu$s, shots total 0.770 s, $T_{\rm run} = 0.820$ s.

Ion: the compiled circuit is `rx` on q0 (10 μs), `ms` (200 μs), and two single-qubit dressing
gates (T02 §2.3) that land on *different* ions, so the ASAP schedule (1.2) puts them in one
10 μs layer: $T_{\rm circ} = 10 + 200 + 10 = 220\ \mu$s; per shot
$1500 + 220 + 300 + 100 = 2\,120\ \mu$s; shots 2.171 s; $T_{\rm run} = 2.27$ s.

Fidelity (§4): transmon $F\approx(1-3.5\times10^{-4})(1-7.5\times10^{-3})(1-0.02)^2\,(1-\epsilon_{\rm idle})$
with idle on q1 during the `sx` (32 ns, (4.2): $\epsilon_{\rm idle} = 32\,{\rm ns}/(3\cdot 95\,\mu{\rm s}) +
32\,{\rm ns}/(6\cdot150\,\mu{\rm s}) = 1.5\times10^{-4}$)
$= 0.99965\times0.9925\times0.9604\times0.99985 = 0.953$. Ion (three single-qubit pulses, one
`ms`, two readouts, idle negligible): $(1-10^{-4})^3(1-4\times10^{-3})(1-0.003)^2 = 0.990$.

### 2.2 Grover search, 3 qubits, marked state $\lvert 101\rangle$, 4096 shots

Optimal iterations $k = \lfloor\frac\pi4\sqrt{8}\rfloor = 2$. Each iteration: oracle (one
$CCZ$) and diffusion ($H^{\otimes3}X^{\otimes3}\,CCZ\,X^{\otimes3}H^{\otimes3}$), i.e. two
Toffoli-class gates per iteration, 4 in total. A Toffoli on a line of three qubits (heavy-hex
gives no triangle) costs 8 CX with the middle qubit as the shared neighbour (6 CX for the
Toffoli plus routing of one interaction, or the 8-CX linear-nearest-neighbour Toffoli), so
$G_2 = 32$, $G_1\approx 60$ pulses. Scheduled depth: 32 CX layers (the CXs are sequential on
three qubits) plus $\approx 30$ single-qubit layers not hidden behind CX.

Transmon: $T_{\rm circ}\approx 32\times380 + 30\times32 = 12\,160 + 960 = 13.12\ \mu$s; per shot
$0.872 + 13.12 + 0.8 + 1.0 = 15.79\ \mu$s; shots $4096\times15.79\ \mu{\rm s} = 64.7$ ms;
$T_{\rm run} = 50 + 64.7 = 115$ ms. Fidelity:
$(1-7.5\times10^{-3})^{32}(1-3.5\times10^{-4})^{60}(1-0.02)^{3}\,(1-\epsilon_{\rm idle})^3$ with total
idle per qubit $\approx 5\ \mu$s (each CX leaves the third qubit idle) $\Rightarrow$ per qubit
(4.2) $\epsilon_{\rm idle} = 5/(3\cdot95) + 5/(6\cdot150) = 0.0231$:
$0.786\times0.979\times0.941\times0.934 = 0.68$ — by (4.3) the estimator reports success
probability of $\lvert101\rangle$ $\approx 0.68\times0.945 + (1-0.68)/8 = 0.68$ (uniform error
model), versus the ideal $\sin^2(5\theta) = 0.945$.

Ion (all-to-all, no routing): $G_2 = 24$ `ms` (6 per Toffoli), $G_1\approx 60$;
$T_{\rm circ} = 24\times200 + 60\times10 = 5.4$ ms; per shot $1.5 + 5.4 + 0.3 + 0.1 = 7.3$ ms;
shots $4096\times7.3$ ms $= 29.9$ s; $T_{\rm run} = 30.0$ s. Fidelity $(1-4\times10^{-3})^{24}
(1-10^{-4})^{60}(1-0.003)^3 = 0.908\times0.994\times0.991 = 0.89$ (idle negligible: $T_2\sim 1$ s).

### 2.3 Random circuit, 27 qubits, depth 100, 1024 shots

Each layer: 13 CX on disjoint edges plus 27 single-qubit gates. $G_2 = 1\,300$, $G_1 = 2\,700$.

Transmon: $T_{\rm circ} = 100\times(380 + 32) = 41.2\ \mu$s; per shot $0.872 + 41.2 + 0.8 + 1.0 =
43.9\ \mu$s; shots $44.9$ ms; $T_{\rm run} = 94.9$ ms. Fidelity
$(1-7.5\times10^{-3})^{1300}(1-3.5\times10^{-4})^{2700}(0.98)^{27} = 5.6\times10^{-5}\times0.389\times0.580
= 1.3\times10^{-5}$: the estimator reports "output indistinguishable from uniform at this depth;
$F_{\rm XEB}$ expected $\approx 10^{-5}$". Ion chain of 27 (if such a device were configured):
$T_{\rm circ} = 100\times(200 + 10)\ \mu{\rm s} = 21$ ms; per shot $1.5 + 21 + 0.3 + 0.1 = 22.9$ ms;
shots 23.4 s; fidelity $(0.996)^{1300}(0.9999)^{2700}(0.997)^{27} = 5.5\times10^{-3}\times0.763\times0.922
= 3.9\times10^{-3}$.

### 2.4 Same circuits, classical cost

State-vector simulation of the 27-qubit circuit (T11 §11): memory $16\times 2^{27} = 2$ GiB;
after fusion $\approx 1\,000$ kernel passes at $32\times2^{27}/(100\ {\rm GB/s}) = 43$ ms each
$\Rightarrow\approx 43$ s, plus sampling ($\ll 1$ s). The hardware run (45 ms of shots) is
$10^3\times$ faster in shot time but the answer it returns has fidelity $10^{-5}$. For the
Bell and Grover circuits the simulator is faster than the hardware by the full $T_{\rm load}$.

## 3. Classical simulation cost and crossover

Simulation time for a state-vector run (T11 (11.1)):
$$
T_{\rm sim}\approx \frac{G}{f}\cdot\frac{32\cdot 2^n}{B} + T_{\rm sample}, \tag{3.1}
$$
with fusion factor $f\approx 4$ and memory bandwidth $B$. Hardware shot time
$T_{\rm hw} = N_{\rm shots}(T_{\rm reset} + T_{\rm circ} + T_{\rm ro} + T_{\rm gap})$. The
crossover qubit count at which $T_{\rm sim} = T_{\rm hw}$ for a depth-$D$ circuit is
$$
n^\star = \log_2\!\left(\frac{f\,B\,N_{\rm shots}\,(T_{\rm reset} + D\,t_{\rm layer} + T_{\rm ro} + T_{\rm gap})}{32\,G}\right). \tag{3.2}
$$
With $B = 100$ GB/s, $N_{\rm shots} = 1024$, $D = 100$, $t_{\rm layer} = 412$ ns, $G = 4\,000$
(per-shot time $43.9\ \mu$s from §2.3) this gives
$n^\star = \log_2(4\cdot10^{11}\cdot1024\cdot 43.9\times10^{-6}/1.28\times10^{5})
= \log_2(1.4\times10^5)\approx 17$ — but the memory wall arrives first at
$n\approx 28$–$30$, where simulation stops being possible on the workstation at all. The
report shows both numbers: the crossover from (3.2) and the memory limit; the user-facing
statement is a table, not a verdict.

## 4. Fidelity estimate from calibration

### 4.1 The product model

Assume every error event is independent and any error spoils the result (worst case for
success probability, standard for "circuit fidelity"):
$$
F_{\rm est} = \prod_{g\in\text{gates}}(1-\epsilon_g)\;\prod_{q}\prod_{\text{idle segments } s\text{ of }q}\big(1-\epsilon_{\rm idle}(t_s; T_{1,q}, T_{2,q})\big)\;\prod_{q\in\text{measured}}(1-\epsilon_{{\rm ro},q}), \tag{4.1}
$$
with $\epsilon_g$ the RB average error rate of the gate on the *physical* qubits it was
routed to (edge-specific for two-qubit gates), and the idle error the average infidelity of
the thermal-relaxation channel over a segment of length $t$ (derivation: PTM diagonal
$(1, e^{-t/T_2}, e^{-t/T_2}, e^{-t/T_1})$, $F_e = \mathrm{Tr}R/4$, $\bar F = (2F_e+1)/3$):
$$
\epsilon_{\rm idle}(t) = 1 - \bar F = \frac{2\left(1-e^{-t/T_2}\right) + \left(1 - e^{-t/T_1}\right)}{6}
\;\approx\; \frac{t}{3T_2} + \frac{t}{6T_1}\quad (t\ll T_1,T_2). \tag{4.2}
$$
Idle segments are read off the ASAP schedule: every interval in which a qubit has no gate,
from its first gate to its measurement. Gates already include their own decoherence in
$\epsilon_g$ (RB measures it), so idle is counted only between gates. Readout error uses the
diagonal of the assignment matrix (T10 §7) for the state the qubit is most likely in, or the
average $\tfrac12(\epsilon_{01}+\epsilon_{10})$ when unknown.

Success probability for a target outcome $x^\star$ with ideal probability $p^\star$ is
estimated under the uniform-error model as
$$
P(x^\star)\approx F_{\rm est}\,p^\star + (1-F_{\rm est})\,2^{-n_{\rm meas}}. \tag{4.3}
$$

### 4.2 Known biases (displayed with the estimate)

- **Coherent errors** add in amplitude, not probability: $k$ identical over-rotations by
  $\delta$ give error $\sim(k\delta)^2$, not $k\delta^2$; the product model can underestimate by
  up to a factor $k$. RB-derived $\epsilon_g$ hide this (T10 §1.5).
- **Correlated errors** (crosstalk, ZZ during idles, TLS fluctuators) are not independent
  across qubits; ZZ on idling neighbours is partially included through the calibration's
  $\zeta$: an idle pair accumulates the conditional phase $\phi_{ZZ} = \zeta t$, which the
  estimator counts as an additional error $\sin^2(\zeta t/2)/2$ per idle pair when
  `include_zz` is on (default on).
- **Error cancellation / benign errors**: many errors do not change the measured bit-string
  (e.g. a $Z$ error immediately before a $Z$ measurement); (4.1) counts them as failures.
  For algorithms whose output is a single bit-string this makes (4.1) pessimistic by a factor
  that can approach 2.
- **Leakage** to $\lvert 2\rangle$ is counted inside $\epsilon_g$ by RB only partially.

### 4.3 The alternative: simulate the noise

For $n\le 13$ the density-matrix backend, and for larger $n$ Pauli-sampled trajectories,
run the compiled circuit under the calibration-derived noise model (spec `08`). The
estimator then reports the **Hellinger fidelity** between the noisy and ideal output
distributions,
$$
F_H = \Big(\sum_x\sqrt{p_{\rm noisy}(x)\,p_{\rm ideal}(x)}\Big)^2, \tag{4.4}
$$
and the direct success probability $p_{\rm noisy}(x^\star)$, both with the statistical
uncertainty of the shot count. The product model (4.1) and the simulated value are shown
side by side; a discrepancy larger than 2× triggers a note naming the likely cause (coherent
or correlated errors in the model that the product cannot capture).

For random-circuit workloads the simulated $F_{\rm XEB}$ (T10 §5) is compared against
$\prod_g(1-\epsilon_g)$, the depolarizing prediction.

## 5. Algorithm scaling on hardware

The estimator evaluates the following closed forms for the parametrised algorithm library
(spec `15 §5`) so the user can see how time and fidelity scale with problem size without
compiling each instance.

### 5.1 Grover

Search space $N = 2^n$, $M$ marked items: $k = \lfloor\frac\pi4\sqrt{N/M}\rfloor$ iterations,
success probability $\sin^2\!\big((2k+1)\theta\big)$, $\theta = \arcsin\sqrt{M/N}$. Each iteration
costs one oracle call plus the diffusion operator, an $n$-controlled $Z$: with $n-2$ clean
ancillas it decomposes into $2n-3$ Toffolis (depth $\approx 2n$ Toffoli layers, each Toffoli
$= 6$ CX and depth 12 in CX+1q layers on full connectivity); without ancillas $O(n^2)$ CX.
Hardware time per shot
$$
T_{\rm circ}^{\rm Grover}\approx k\,\big(D_{\rm oracle} + 2n\cdot 12\big)\, t_{\rm layer}, \tag{5.1}
$$
and fidelity $\approx(1-\epsilon_2)^{k\,(G_2^{\rm oracle} + 6(2n-3))}$ — for $\epsilon_2 = 10^{-2}$ the
fidelity drops below $1/2$ once $k\,G_2 > 69$, i.e. Grover beyond $n\approx 4$ is not
useful on today's uncorrected devices; the estimator says exactly this with the numbers.

### 5.2 Quantum Fourier transform and phase estimation

QFT on $n$ qubits: $n(n-1)/2$ controlled-phase gates $+ n$ Hadamards, depth $O(n)$ with
nearest-neighbour ordering and $\lfloor n/2\rfloor$ swaps; approximate QFT drops rotations
smaller than $\pi/2^{m}$ with $m = \log_2 n + 2$, leaving $O(n\log n)$ gates. Phase estimation
with $m$ precision bits on a unitary $U$ with controlled-$U^{2^j}$ cost $C_U(j)$:
$G = \sum_{j<m} C_U(j) + G_{\rm QFT}(m)$; for a $U$ implemented by repetition,
$\sum_j 2^j C_U = (2^m - 1)C_U$.

### 5.3 Shor (order finding)

For an $n$-bit modulus (RSA-2048: $n = 2048$), the reference construction (Gidney–Ekerå
2019, windowed arithmetic, coset representation) uses
$$
N_{\rm Toffoli}\approx 0.3\,n^3 + 0.0005\,n^3\log_2 n,\qquad
Q_L\approx 3n + 0.002\,n\log_2 n, \tag{5.2}
$$
giving $2.7\times10^9$ Toffolis and $6\,189$ logical qubits at $n=2048$; the textbook
construction (Beauregard 2003) uses $2n+3$ qubits and $O(n^3\log n)$ gates with a much larger
constant. Each Toffoli costs 4 T gates (with a measurement-based construction) or 7 T
(Clifford+T decomposition), so $N_T\approx 4N_{\rm Toffoli}$ when T-counts are requested.
Uncorrected hardware: $F\approx(1-\epsilon_2)^{6N_{\rm Toffoli}}$ is $10^{-10^{7}}$-class for any
$n > 8$; the estimator reports Shor beyond trivial sizes as "requires error correction" and
switches to §6.

### 5.4 Variational circuits (VQE, QAOA)

Time per energy evaluation $= N_{\rm shots}\times N_{\rm Pauli\ groups}\times$ (per-shot time of
the ansatz circuit); a hardware-efficient ansatz of $L$ layers on $n$ qubits has
$G_2 = L(n-1)$ (linear entangler) and $G_1 = 2nL$. QAOA with $p$ layers on a graph with $|E|$
edges: $G_2 = 2p|E|$ (each $R_{ZZ}$ = 2 CX) plus routing overhead $\approx 1.5$–$3\times$ on
sparse connectivity. The optimizer's iteration count ($10^2$–$10^4$) multiplies the total;
the estimator takes it as an input with default 200.

## 6. Error-corrected regime

When the uncorrected fidelity of §4 falls below a user threshold (default 0.5), the estimator
switches to the surface-code model of T09 §7–§9 and reports:

$$
d = 2\left\lceil\frac{\ln\!\big(p_L^{\rm target}/A\big)}{\ln(p/p_{th})}\right\rceil - 1,\qquad
p_L^{\rm target} = \frac{\epsilon}{Q_L\,N_{\rm cyc}}, \tag{6.1}
$$
$$
Q_{\rm phys} = 1.5\,Q_L\,(2d^2-1) + N_F\cdot 72\,d_f^2,\qquad
N_F = \left\lceil\frac{6\,d_f}{d}\right\rceil\ (\text{one T per logical cycle}), \tag{6.2}
$$
$$
T_{\rm QEC} = N_{\rm cyc}\,d\,t_c,\qquad N_{\rm cyc} = \max\!\left(N_T + D_C,\ \frac{N_T\cdot 6d_f}{N_F\,d}\right)\cdot\frac{1}{\text{(T-layer parallelism)}}, \tag{6.3}
$$
with $p$ the device's two-qubit error rate (the dominant one), $t_c$ the syndrome cycle time
from the device's gate and readout durations (T09 §5.2), $A = 0.1$, $p_{th} = 10^{-2}$,
$d_f = d$ unless the user sets a separate factory distance. The T-layer parallelism (number
of T gates that can be executed in the same logical cycle, limited by factories and by the
algorithm's T-depth) defaults to 1.

**Canonical output — RSA-2048 on a transmon device with $p = 10^{-3}$, $t_c = 1\ \mu$s:**
$Q_L = 6\,189$, $N_{\rm Toffoli} = 2.7\times10^{9}$ (treated as $N_{\rm cyc}$ with Toffoli
factories at one per cycle, matching the reference layout), $\epsilon = 0.06$: $p_L^{\rm target}
= 3.6\times10^{-15}$, $d = 27$; data $1.5\times6189\times1457\approx 1.35\times10^{7}$ qubits;
factories (Toffoli factories are larger than 15-to-1: the reference uses $\approx 5\times10^{6}$
qubits of factory space) $\Rightarrow Q_{\rm phys}\approx 2\times10^{7}$; time $2.7\times10^{9}\times
27\times 1\ \mu{\rm s} = 7.3\times10^{4}$ s $\approx 20$ h at one Toffoli per $d$ cycles, or $\approx 8$ h
with the reference's 2.5 Toffolis per cycle-window pipelining. The estimator reports "8–20 h,
$\sim 2\times10^{7}$ physical qubits" and cites the assumptions. With $p = 10^{-4}$ (ion-trap
class two-qubit error) $d = 13$ and $Q_{\rm phys}\approx 4\times10^{6}$, but $t_c\approx 1$ ms
gives $2.7\times10^{9}\times 13\times 1\ {\rm ms} = 3.5\times10^{7}$ s $\approx 1.1$ years at the
same cycle count: five times fewer qubits, a thousand times longer. The estimator shows both axes.

## 7. Uncertainty propagation

Calibration files carry a `[value, sigma, source]` triple (value and 1σ) for every parameter
(spec `09 §3`); min/max are taken as value ∓ 2σ.
The estimator evaluates (1.1), (4.1), and (6.1)–(6.3) three times — with all parameters at
their favourable, typical, and unfavourable ends — and reports the resulting interval as
"typ (min–max)". Correlations are ignored (the interval is conservative). For (4.1) the
error-rate uncertainties are propagated in the log domain: $\ln F = \sum\ln(1-\epsilon)$,
$\sigma^2_{\ln F} = \sum\big(\sigma_\epsilon/(1-\epsilon)\big)^2$, and the 1σ band is shown
alongside the min–max interval.

## 8. Comparison table format

Every estimate is presented as one row per device plus one row for the local simulator:

| Column | Content |
|--------|---------|
| Device | name, technology, qubit count, connectivity |
| Compiled | $n_{\rm used}$, $G_1$, $G_2$, depth, SWAPs inserted |
| Shot time | per shot (reset + circuit + readout + gap) |
| Total time | (1.1) with $T_{\rm load}$ shown separately |
| Fidelity (model) | (4.1) with interval |
| Fidelity (simulated) | (4.4) when available, with shot uncertainty |
| Success probability | (4.3) or simulated |
| QEC | "not needed" / $d$, $Q_{\rm phys}$, $T_{\rm QEC}$ from §6 |
| Class | `Model` (always), with the assumption list of §9 attached |

## 9. Assumption list (displayed verbatim with every estimate)

1. Gate durations, error rates, coherence times, and readout parameters are those of the
   selected device's calibration file at its timestamp; drift is not modelled.
2. Circuit time is the ASAP critical path of the compiled circuit; the control system is
   assumed to execute parallel gates simultaneously.
3. Reset uses the device's declared method; passive reset waits $5\,T_1$.
4. Job overhead and inter-shot gap are fixed constants from the device file; queue time is
   not included.
5. Errors are independent; gate errors are depolarizing at the RB rate; idle errors follow
   (4.2); readout errors use the assignment matrix diagonal.
6. Coherent, correlated, and leakage errors are not captured by the product model; the
   simulated fidelity, when shown, includes those present in the noise model only.
7. Error-corrected figures use the rotated surface code, MWPM-class decoding, $A = 0.1$,
   $p_{th} = 1\%$, one logical cycle $= d$ syndrome rounds, 15-to-1 (or Toffoli) factories with
   the footprint and rate of T09 §8.3, and a routing overhead factor 1.5.
8. Classical simulation time uses the memory-bandwidth model of T11 §11 with this
   workstation's measured bandwidth.

## Where this is used

| Result | Spec |
|--------|------|
| Wall-time model (1.1)–(1.3), parameter table | `15 §6` `runtime::Estimator`; device files `09 §2`, `09 §4` |
| Worked examples (§2) | conformance values `25 §5`; example programs `Assets/Programs/estimation/` |
| Crossover (3.1)–(3.2) | `15 §3` comparison row; `24 §2` |
| Product fidelity model (4.1)–(4.3), idle error (4.2) | `15 §4`; noise-model idle channel `08 §3` |
| Bias list (§4.2), Hellinger (4.4) | `15 §4` UI notes; `22 §6` |
| Algorithm scaling (§5) | `15 §5` algorithm library |
| QEC regime (6.1)–(6.3), RSA-2048 canonical output | `15 §6`, `16 §5` |
| Uncertainty propagation (§7) | `15 §9`, `09 §3` calibration schema |
| Table format (§8), assumption list (§9) | `19 §6` estimate panel; `23 §4` report export |
