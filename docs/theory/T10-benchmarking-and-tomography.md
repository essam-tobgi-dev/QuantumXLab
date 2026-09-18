# T10 — Benchmarking, Tomography and Calibration Protocols

This document defines the fidelity measures the application reports, the benchmarking
protocols the instrument and analysis modules implement, and the standard calibration
experiments (Rabi, Ramsey, $T_1$, echo, DRAG, ZZ, readout) with their fit models. It is the
reference for the "Physical lab" workflow, in which the user learns about the device only
through these protocols.

Notation: $d = 2^n$ is the Hilbert-space dimension; $\mathcal E$ is a completely positive
trace-preserving (CPTP) map; $\mathcal U(\rho) = U\rho U^\dagger$ is the ideal gate.

## 1. Fidelity measures

### 1.1 State fidelity

For density operators $\rho, \sigma$ the Uhlmann fidelity is
$$
F(\rho,\sigma) = \left(\mathrm{Tr}\sqrt{\sqrt\rho\,\sigma\sqrt\rho}\right)^2 , \tag{1.1}
$$
which reduces to $F = \langle\psi|\sigma|\psi\rangle$ for pure $\rho = |\psi\rangle\langle\psi|$ and
to $|\langle\psi|\phi\rangle|^2$ for two pure states. The application uses this squared
convention throughout (some literature reports $\sqrt F$). For two classical distributions
$P, Q$ over outcomes the same quantity is the squared Bhattacharyya coefficient, and its
complement defines the **Hellinger distance** $H^2 = 1 - \sum_x\sqrt{P(x)Q(x)}$, used to compare
measured and ideal histograms (T12 §4).

### 1.2 Entanglement (process) fidelity

Let $|\Phi\rangle = \frac{1}{\sqrt d}\sum_{j}|j\rangle|j\rangle$ be the maximally entangled state on
two copies of the system. The entanglement fidelity of $\mathcal E$ with respect to the ideal
$\mathcal U$ is
$$
F_e(\mathcal E,\mathcal U) = \langle\Phi|\,(\mathcal U^\dagger\!\circ\mathcal E\otimes\mathcal I)\big(|\Phi\rangle\langle\Phi|\big)\,|\Phi\rangle
= \frac{1}{d^2}\sum_k \left|\mathrm{Tr}\,(U^\dagger K_k)\right|^2 , \tag{1.2}
$$
where $K_k$ are Kraus operators of $\mathcal E$. Equivalently $F_e = \frac{1}{d^2}\mathrm{Tr}\,(\mathcal
U^\dagger \mathcal E)$ in the Pauli-transfer-matrix representation.

### 1.3 Average gate fidelity

The average over Haar-random pure input states
$$
\bar F(\mathcal E,\mathcal U) = \int d\psi\;\langle\psi|U^\dagger\mathcal E(|\psi\rangle\langle\psi|)U|\psi\rangle
= \frac{d\,F_e + 1}{d+1} \tag{1.3}
$$
(Horodecki et al. 1999; Nielsen 2002). The average **error rate** is $r = 1 - \bar F$.
For $d=2$: $\bar F = (2F_e+1)/3$; for $d=4$: $\bar F = (4F_e+1)/5$.

### 1.4 Depolarizing channel and its parameters

The $d$-dimensional depolarizing channel is
$$
\mathcal D_p(\rho) = (1-p)\rho + p\,\frac{I}{d}. \tag{1.4}
$$
Its entanglement fidelity is $F_e = (1-p) + p/d^2$ (the identity term contributes 1, the
maximally mixed term contributes $\langle\Phi|\tfrac{I}{d}\otimes I|\Phi\rangle = 1/d^2$).
Inserting into (1.3): $d F_e + 1 = d - dp + p/d + 1 = (d+1) - p(d - 1/d) = (d+1) - p\frac{(d-1)(d+1)}{d}$,
hence
$$
r = 1-\bar F = p\,\frac{d-1}{d}, \qquad p = \frac{d}{d-1}\, r . \tag{1.5}
$$
For $d=2$: $p = 2r$ (an error rate $r=10^{-3}$ corresponds to depolarizing probability
$2\times10^{-3}$, i.e. each Pauli $X,Y,Z$ applied with probability $r\cdot 2/3$). For $d=4$:
$p = \tfrac43 r$, i.e. each of the 15 non-identity two-qubit Paulis with probability $p/15 =
4r/45$. The alternative *Pauli-form* parametrisation $\mathcal D(\rho) = (1-p')\rho +
\frac{p'}{d^2-1}\sum_{P\neq I}P\rho P$ is related by $p' = p\,(d^2-1)/d^2$. The application
stores $r$ in calibration files and converts with (1.5); the conversion is shown in the
inspector.

### 1.5 Diamond norm

The worst-case error $\epsilon_\diamond = \tfrac12\|\mathcal E - \mathcal U\|_\diamond$ bounds the
distinguishability of the noisy and ideal gates over any input, including entangled
ancillas. For purely stochastic (Pauli) errors $\epsilon_\diamond = r\,(d+1)/d$; for coherent
errors $\epsilon_\diamond$ can be as large as $\sqrt{r\,d(d+1)}$ — up to $\sqrt r$ scaling. This
is why RB error rates underestimate the damage coherent errors do in an algorithm (T12 §4).

## 2. Randomized benchmarking

### 2.1 Twirling

For any CPTP map $\mathcal E$, averaging over the Clifford group,
$$
\overline{\mathcal E} = \frac{1}{|\mathcal C|}\sum_{C\in\mathcal C}\mathcal C^\dagger\circ\mathcal E\circ\mathcal C = \mathcal D_p, \tag{2.1}
$$
is exactly depolarizing, because the Clifford group is a unitary 2-design. Twirling preserves
$F_e$ and therefore $\bar F$, so the depolarizing parameter of $\overline{\mathcal E}$ is
$p = \frac{d}{d-1}\,(1-\bar F(\mathcal E))$ by (1.5).

### 2.2 Standard protocol

For sequence length $m$: sample $m$ uniformly random Cliffords $C_1\ldots C_m$, append
$C_{m+1} = (C_m\cdots C_1)^{-1}$, prepare $|0\rangle^{\otimes n}$, run, measure survival
probability $P(\text{return to }|0\rangle)$. Average over $K$ random sequences per $m$. Under
gate-independent noise the survival decays as
$$
F(m) = A\,p^m + B , \tag{2.2}
$$
where $A, B$ absorb state-preparation and measurement (SPAM) errors and $p$ is the
depolarizing parameter of the average Clifford. The error per Clifford is
$$
r_C = (1-p)\,\frac{d-1}{d}. \tag{2.3}
$$
With gate-dependent noise a first-order correction adds a term $C(m-1)p^{m-2}$ (Magesan et
al. 2012); the application fits (2.2) and reports the residual as a diagnostic.

**Error per native gate.** Cliffords are compiled to native gates; the average count per
Clifford in the $\{R_z, \sqrt X\}$ convention is $1.875$ physical pulses ($\sqrt X$ counted,
$R_z$ virtual) for single-qubit Cliffords (the 24 elements need 0, 1, or 2 $\sqrt X$ pulses:
$(1\cdot 0 + 12\cdot 1+ 11\cdot 2)/24 = 1.4583$ $\sqrt X$ gates, and $1.875$ when $R_z$ frame
updates are counted as gates). For the two-qubit Clifford group (11 520 elements) the average
is $1.5$ CNOTs plus $\approx 3.3$ single-qubit gates per qubit using the standard
decomposition. The application reports $r_{\rm gate} = r_C / N_{\rm gates\,per\,Clifford}$ with the
compilation it actually used, and the two-qubit figure is quoted as *error per Clifford* and
per-CNOT after interleaved RB (§2.3).

### 2.3 Interleaved RB

Run a reference experiment ($p_{\rm ref}$) and an experiment in which the gate of interest $G$
is inserted after every random Clifford ($p_{\rm int}$). Then
$$
r_G = \left(1 - \frac{p_{\rm int}}{p_{\rm ref}}\right)\frac{d-1}{d}, \tag{2.4}
$$
with systematic bounds (Magesan et al. 2012)
$$
|r_G - r_G^{\rm est}| \le \min\!\left\{ \frac{(d-1)\left[|p_{\rm ref}-p_{\rm int}/p_{\rm ref}| + (1-p_{\rm ref})\right]}{d},\;
\frac{2(d^2-1)(1-p_{\rm ref})}{p_{\rm ref}\,d^2} + \frac{4\sqrt{1-p_{\rm ref}}\sqrt{d^2-1}}{p_{\rm ref}}\right\}. \tag{2.5}
$$
The application displays $r_G$ with the bound (2.5) as its systematic uncertainty.

### 2.4 Simultaneous RB

Running single-qubit RB on all qubits at once and comparing per-qubit $r$ with the isolated
value measures crosstalk: $r^{\rm sim}_q - r^{\rm iso}_q$ is the addressability error of qubit
$q$. Correlated RB extends this with a pair fit (Gambetta et al. 2012).

### 2.5 Statistical treatment

Default design: lengths $m\in\{1, 2, 4, 8, 16, 32, 64, 128, 256, 512\}$ for single-qubit,
$\{1,2,4,8,16,32,64\}$ for two-qubit, $K = 30$ sequences per length, $N = 1024$ shots per
sequence. The survival probability per sequence has binomial variance $F(1-F)/N$; the
sequence-to-sequence variance (from the random Cliffords) dominates and is estimated
directly from the $K$ samples. Fit (2.2) by weighted nonlinear least squares (spec `22 §5`);
report $p$ with its 1σ from the covariance and $r_C$ by (2.3). A Wilson score interval is used
for each individual survival probability when $N F < 5$.

## 3. State tomography

### 3.1 Pauli expectations and linear inversion

Any $n$-qubit density operator expands in the Pauli basis:
$$
\rho = \frac{1}{2^n}\sum_{P\in\{I,X,Y,Z\}^{\otimes n}} \langle P\rangle\,P, \qquad \langle P\rangle = \mathrm{Tr}(\rho P). \tag{3.1}
$$
Each $\langle P\rangle$ is measured by rotating every qubit into the eigenbasis of its factor
($H$ for $X$, $S^\dagger H$ for $Y$, nothing for $Z$) and measuring in the computational basis;
one setting yields all $P$ whose non-identity factors match the setting, so $3^n$ settings
suffice (each with $N$ shots). Linear inversion inserts the estimated $\langle P\rangle$ into
(3.1). The result is Hermitian with unit trace but may have negative eigenvalues from
statistical noise.

### 3.2 Maximum likelihood

Parametrise a physical state by a lower-triangular complex matrix $T$ with real diagonal:
$$
\rho(T) = \frac{T^\dagger T}{\mathrm{Tr}(T^\dagger T)}, \tag{3.2}
$$
which is positive semidefinite and unit trace by construction ($4^n - 1$ real parameters).
For measurement settings $s$ with outcomes $x$ observed $N_{s,x}$ times and outcome
projectors $\Pi_{s,x}$, minimize the negative log-likelihood
$$
-\ln\mathcal L(T) = -\sum_{s,x} N_{s,x}\,\ln\mathrm{Tr}\big(\rho(T)\,\Pi_{s,x}\big) \tag{3.3}
$$
by L-BFGS or the iterative $R\rho R$ algorithm. Report the MLE state, its fidelity to the
target, and the log-likelihood ratio to the linear-inversion state. Readout errors are
folded in by replacing $\Pi_{s,x}$ with $\sum_{x'} M_{x x'}\Pi_{s,x'}$ using the assignment matrix
of §7.

Practical limits: $n\le 4$ in the application (81 settings, $3^4\cdot N$ shots); the estimated
number of shots for $\pm 0.01$ on each expectation value is $N\approx 10^4$ per setting.

## 4. Process tomography

### 4.1 $\chi$-matrix representation

With a fixed operator basis $\{E_m\}$ (Paulis, $m = 0..d^2-1$), any CPTP map is
$$
\mathcal E(\rho) = \sum_{m,n=0}^{d^2-1} \chi_{mn}\,E_m\,\rho\,E_n^\dagger , \tag{4.1}
$$
with $\chi$ Hermitian, positive semidefinite, and $\sum_{mn}\chi_{mn}E_n^\dagger E_m = I$
(trace preservation). $F_e = \chi_{00}$ when $E_0 = U^\dagger$-rotated identity, i.e. after
composing with the inverse ideal gate. The **Pauli transfer matrix** $R_{ij} =
\frac1d\mathrm{Tr}[P_i\,\mathcal E(P_j)]$ is the alternative real representation used for
display ($d^2\times d^2$ real entries in $[-1,1]$).

### 4.2 Protocol

Prepare each of $d^2$ linearly independent input states (per qubit: $|0\rangle, |1\rangle,
|+\rangle, |{+i}\rangle$), apply the process, and perform state tomography on each output:
$4^n\times 3^n$ settings ($n=1$: 12; $n=2$: 144). Reconstruct by linear inversion (solve the
linear system for $\chi$) or by MLE with $\chi = T^\dagger T$ and the trace-preservation
constraint enforced by a penalty term. Report $F_e$, $\bar F$ from (1.3), and the PTM.

### 4.3 Gate-set tomography (reference only)

Self-consistently estimates the gate set, state preparation, and measurement without assuming
any of them; uses germ sequences of increasing length and a long-sequence fit. Out of scope
for v1; the application points to pyGSTi-style analysis in the theory viewer.

## 5. Cross-entropy benchmarking

For a random circuit $U$ on $n$ qubits, the ideal output probabilities $p_U(x) =
|\langle x|U|0\rangle|^2$ follow the Porter–Thomas distribution $\Pr(p) = d\,e^{-dp}$ for large $n$.
The linear cross-entropy fidelity is estimated from sampled bit-strings $x_1\ldots x_N$:
$$
F_{\rm XEB} = d\,\big\langle p_U(x_i)\big\rangle_i - 1 . \tag{5.1}
$$
Ideal sampling gives $F_{\rm XEB}\to 1$ (since $d\,\mathbb E_{p}[p] = d\sum_x p(x)^2 \to 2$ for
Porter–Thomas); uniform sampling gives 0. Under depolarizing noise $F_{\rm XEB}\approx
\prod_g (1 - \epsilon_g)$. The application computes $p_U(x)$ from the state-vector backend
(Simulator-only for $n$ beyond tomography reach) and uses XEB as the cross-check of the
estimator's fidelity model (T12 §4). The standard error of (5.1) is $\approx
\sqrt{(1+2F_{\rm XEB} - F_{\rm XEB}^2)/N}$ for Porter–Thomas statistics.

## 6. Quantum volume

Protocol (Cross et al. 2019): for width $n$, generate random circuits of $n$ layers, each layer
a random permutation followed by random $SU(4)$ gates on qubit pairs. For each circuit compute
the ideal distribution, define **heavy outputs** as bit-strings with $p(x) > $ median$\{p\}$,
and run the compiled circuit on the device. The test passes at width $n$ if the fraction of
heavy outputs $h$ across $\ge 100$ circuits satisfies
$$
h - 2\sigma_h > \tfrac23, \qquad \sigma_h = \sqrt{h(1-h)/N_{\rm circuits}} \tag{6.1}
$$
(the ideal heavy-output probability tends to $(1+\ln 2)/2\approx 0.847$; a fully depolarized
device gives $1/2$). Quantum volume is $QV = 2^{n_{\max}}$ for the largest passing $n$; the
application reports $\log_2 QV$ and the heavy-output fraction per width.

## 7. Readout calibration

### 7.1 Assignment matrix

Prepare each computational basis state $|y\rangle$ (for $n$ qubits, $2^n$ preparations),
measure $N$ times, and estimate
$$
M_{xy} = \Pr(\text{measured } x \mid \text{prepared } y), \qquad \sum_x M_{xy} = 1. \tag{7.1}
$$
For a single qubit $M = \begin{pmatrix}1-\epsilon_{01} & \epsilon_{10}\\ \epsilon_{01} & 1-\epsilon_{10}\end{pmatrix}$
with $\epsilon_{01}$ = probability of reading 1 when 0 was prepared. Typical superconducting
values: $\epsilon_{01}\approx 0.5$–$2\%$, $\epsilon_{10}\approx 1$–$5\%$ ($T_1$ decay during
readout makes $\epsilon_{10} > \epsilon_{01}$). Trapped ions: $10^{-3}$–$10^{-4}$.

### 7.2 Mitigation

Given measured counts vector $\vec c$, the unmitigated estimate of the true distribution is
$\vec p = M^{-1}\vec c/N$, which can have negative entries. The application instead solves
$$
\min_{\vec p}\ \|M\vec p - \vec c/N\|_2^2 \quad\text{s.t.}\quad p_x\ge 0,\ \sum_x p_x = 1 \tag{7.2}
$$
by projected gradient or SLSQP, and reports both. For $n > 4$ the tensor-product
approximation $M \approx \bigotimes_q M^{(q)}$ ($2^n\to 2n$ parameters) is used, with the
option of measuring the two-qubit correlated matrices for neighbouring pairs.

### 7.3 Readout discriminator

From the digitizer's integrated $(I,Q)$ points for prepared $|0\rangle$ and $|1\rangle$ (spec `12
§4`), fit two Gaussians with means $\mu_0, \mu_1$ and common covariance $\Sigma$; the
discriminator is the linear boundary equidistant in the Mahalanobis metric. Separation
$\mathrm{SNR} = |\mu_1 - \mu_0|/\sigma$ along the connecting axis gives an ideal assignment error
$\epsilon = \tfrac12\mathrm{erfc}\!\left(\mathrm{SNR}/2\sqrt2\right)$ before $T_1$ effects.

## 8. Coherence and calibration experiments

Each experiment below is a parametrised circuit family plus a fit model. The analysis module
(spec `22 §5`) fits by Levenberg–Marquardt with the stated initial guesses.

### 8.1 Rabi (amplitude)

Circuit: $X$-pulse with fixed duration $\tau$ and variable amplitude $A$, then measure.
$$
P_1(A) = \frac{1 - \cos(2\pi\,\Omega' A\,\tau)}{2}\,e^{-\gamma A} + c \tag{8.1}
$$
with $\Omega'$ the Rabi rate per unit amplitude (T07 §2). The $\pi$-pulse amplitude is
$A_\pi = 1/(2\Omega'\tau)$. Grid: 51 amplitudes over $[0, 2A_\pi^{\rm guess}]$. Initial guess for
$\Omega'$ from the FFT peak of $P_1(A)$.

### 8.2 $T_1$ (energy relaxation)

Circuit: $X$, delay $t$, measure.
$$
P_1(t) = a\,e^{-t/T_1} + c . \tag{8.2}
$$
Grid: 31 delays, logarithmically spaced over $[0, 5T_1^{\rm guess}]$ with the first 5 points
linear (captures short-time behaviour and the tail). Reported uncertainty from the fit
covariance; $T_1$ fluctuates by 10–30 % over hours on real transmons (two-level-system
defects), which the calibration file records as a drift range.

### 8.3 Ramsey ($T_2^*$ and frequency)

Circuit: $\sqrt X$, delay $t$ with the drive detuned by $\Delta$ (or a virtual $Z$ rotation
$\phi = 2\pi\Delta t$), $\sqrt X$, measure.
$$
P_1(t) = a\,e^{-t/T_2^*}\cos(2\pi\Delta t + \phi_0) + c \tag{8.3}
$$
(a Gaussian envelope $e^{-(t/T_2^*)^2}$ is fitted instead when the residual favours it, which
indicates low-frequency-dominated dephasing, T04 §6). The qubit frequency error is $\Delta -
\Delta_{\rm set}$. Grid: 51 delays over $[0, 3T_2^{*\,\rm guess}]$ with $\Delta_{\rm set}$ chosen
to give 5–8 oscillations. Detuning both ways ($\pm\Delta_{\rm set}$) removes the sign ambiguity.

### 8.4 Hahn echo ($T_{2}^{\rm echo}$)

Circuit: $\sqrt X$, delay $t/2$, $X$ (or $Y$), delay $t/2$, $\sqrt X$, measure.
$$
P_1(t) = a\,e^{-(t/T_2^{\rm echo})^{\beta}} + c, \quad \beta\in[1,2]. \tag{8.4}
$$
The echo refocuses quasi-static detuning noise, so $T_2^{\rm echo}\ge T_2^*$, bounded by $2T_1$.
CPMG with $N$ $\pi$-pulses extends the protocol and yields the noise spectrum (T04 §6).

### 8.5 DRAG calibration

Sequence: $(X_{\pi/2}, X_{-\pi/2})^{N}$ or the "pseudo-identity" $X\,X^\dagger$ repeated $N$
times with variable DRAG coefficient $\beta$ (T05 §9). Leakage and phase error make $P_1$
depend linearly on $\beta$ around the optimum; fit $P_1(\beta) = a(\beta - \beta_{\rm opt}) + c$ for
$N$ large enough to amplify the error ($N = 5$–$20$). The optimum $\beta_{\rm opt}\approx
-1/\alpha$ ($\alpha$ the anharmonicity in rad/s) is the initial guess.

### 8.6 Amplitude fine-tuning (ping-pong)

Sequence: $X_{\pi/2}$ then $N$ repetitions of $X_\pi$, for $N = 0..20$. Over-rotation by
$\delta$ per pulse produces $P_1(N) = \tfrac12 + \tfrac12(-1)^N\sin(N\delta)$ (approximately); the fitted
slope gives $\delta$ and the amplitude correction $A\to A\,(1 - \delta/\pi)$.

### 8.7 ZZ coupling (conditional Ramsey)

Run the Ramsey experiment on qubit $a$ twice: with qubit $b$ in $|0\rangle$ and in $|1\rangle$
(apply $X_b$ before the sequence). The fitted frequencies differ by the static ZZ rate:
$$
\zeta/2\pi = f_a^{(b=1)} - f_a^{(b=0)} . \tag{8.5}
$$
Typical fixed-frequency transmon pairs: $\zeta/2\pi = 50$–$300$ kHz; tunable-coupler devices
$< 10$ kHz at the idle point (T05 §7).

### 8.8 Crosstalk (drive)

Drive at qubit $a$'s frequency on qubit $b$'s line and run Rabi on $a$: the ratio of Rabi rates
$\Omega_{a\leftarrow b}/\Omega_{a\leftarrow a}$ is the classical crosstalk amplitude (typical
$-20$ to $-40$ dB). The full crosstalk matrix is stored in the calibration file (spec `09 §5`)
and used by the pulse-level backend.

### 8.9 Resonator and qubit spectroscopy

Resonator: sweep the readout tone, fit $|S_{21}(f)|$ with the notch model
$$
S_{21}(f) = a\,e^{i\phi}e^{-2\pi i f\tau}\left[1 - \frac{(Q_l/|Q_c|)\,e^{i\varphi}}{1 + 2iQ_l\,(f-f_r)/f_r}\right] \tag{8.6}
$$
(Probst et al. 2015) for $f_r$, loaded $Q_l$, coupling $Q_c$, and the impedance-mismatch
angle $\varphi$; internal $Q_i^{-1} = Q_l^{-1} - \mathrm{Re}(Q_c^{-1}e^{i\varphi})$. The readout
tone is then placed at $f_r$ (or at the point of maximal state contrast). Qubit: with a weak
drive swept over 3–8 GHz, the dispersive shift moves $f_r$ when the drive hits $f_{01}$; fit
a Lorentzian of width $\Gamma/2\pi = 1/(\pi T_2^*)$ plus power broadening. The $f_{02}/2$
two-photon line at $f_{01} + \alpha/2$ gives the anharmonicity.

## 9. Fit quality and acceptance

Every fit reports: parameters with 1σ, reduced $\chi^2$, and a pass/fail against the
acceptance table used by the automatic calibration sequence (spec `12 §7`):

| Experiment | Accept if |
|-----------|-----------|
| Rabi | $\chi^2_\nu < 3$, fitted contrast $a > 0.6$ |
| $T_1$ | relative 1σ on $T_1$ $< 15\%$ |
| Ramsey | $\ge 3$ visible oscillations, relative 1σ on $\Delta$ $< 5\%$ |
| Echo | $T_2^{\rm echo}\le 2.05\,T_1$ (physicality) |
| RB | $p\in(0,1)$, 1σ on $r_C$ $< 30\%$ of $r_C$ |
| Readout | $M$ column sums $= 1\pm 10^{-9}$, diagonal $> 0.8$ |

## Where this is used

| Result | Spec |
|--------|------|
| Fidelity definitions (1.1)–(1.5), Hellinger | `15 §4` fidelity estimator; `21 §5` state-view readouts; `08 §2` calibration → depolarizing conversion |
| RB protocol, (2.2)–(2.5), design table | `12 §7` benchmarking instrument; `22 §5` fit models; `25 §3` RB oracle test |
| State tomography (3.1)–(3.3) | `12 §8` tomography tool (physical), `21 §2` |
| Process tomography, PTM | `12 §8`, `21 §3.5` (PTM as a city plot; not yet detailed there) |
| XEB (5.1) | `15 §4` cross-check; `25 §3` |
| Quantum volume protocol | `15 §5` device metrics |
| Assignment matrix, mitigation, discriminator (§7) | `08 §5` readout error model; `12 §4` digitizer; `22 §6` |
| Calibration experiments and fit models (§8) | `12 §7` calibration sequence; `22 §5`; `Assets/Programs/calibration/*.qasm` |
| Acceptance table (§9) | `12 §7` |
