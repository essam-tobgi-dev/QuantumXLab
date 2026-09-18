# T04 — Open Quantum Systems and Noise

This document defines every noise process the simulator implements: the channel formalism,
the Lindblad master equation and its assumptions, the specific channels derived from qubit
physics ($T_1$, $T_\varphi$, $T_2$, thermal excitation, depolarizing, leakage, crosstalk,
readout error), how they are combined per gate from calibration data, and how the quantities
are measured (Ramsey, echo). Conventions follow T01: $\sigma^- = |0\rangle\langle1|$ lowers toward
the ground state, $H_q = -\tfrac12\hbar\omega_q Z$.

## 1. Channels

### 1.1 System plus environment

A system $S$ coupled to an environment $E$ evolves unitarily on $\mathcal H_S\otimes\mathcal H_E$.
If the joint initial state is $\rho_S\otimes|e_0\rangle\langle e_0|$, the reduced system
dynamics is

$$
\mathcal E(\rho_S) = \mathrm{Tr}_E\!\left[U(\rho_S\otimes|e_0\rangle\langle e_0|)U^\dagger\right]
= \sum_k K_k\,\rho_S\,K_k^\dagger,\qquad K_k = \langle e_k|U|e_0\rangle,
\tag{1.1}
$$

with $\{|e_k\rangle\}$ any orthonormal basis of $\mathcal H_E$. The operators $K_k$ act on
$\mathcal H_S$ and satisfy $\sum_k K_k^\dagger K_k = I$.

### 1.2 CPTP maps and the operator-sum representation

A **quantum channel** is a linear map $\mathcal E$ on density operators that is completely
positive (CP: $\mathcal E\otimes\mathrm{id}_n$ maps positive operators to positive operators for
every $n$) and trace preserving (TP). Every CPTP map on a $d$-dimensional system has a
**Kraus representation**

$$
\mathcal E(\rho) = \sum_{k=1}^{r} K_k\rho K_k^\dagger,\qquad \sum_k K_k^\dagger K_k = I,\qquad r\le d^2,
\tag{1.2}
$$

and conversely every such sum is CPTP. The representation is unique up to a unitary
remixing $K'_j = \sum_k u_{jk}K_k$ ($u$ unitary, padded with zeros). The simulator stores
channels in Kraus form and verifies (1.2) to $10^{-12}$ on load (spec 08 §2).

### 1.3 Choi matrix

$$
J(\mathcal E) = \sum_{i,j=0}^{d-1}|i\rangle\langle j|\otimes\mathcal E(|i\rangle\langle j|)
\qquad(d^2\times d^2).
\tag{1.3}
$$

$\mathcal E$ is CP iff $J\ge0$ and TP iff $\mathrm{Tr}_2 J = I_d$ (partial trace over the second
factor). The Kraus operators are obtained from the eigen-decomposition
$J = \sum_k|v_k\rangle\langle v_k|$ by reshaping each $|v_k\rangle$ into a $d\times d$ matrix. This
is how the process-tomography output (T10 §4) is converted into a channel the simulator can
apply, and how the $\chi$-matrix representation is related: $J$ and $\chi$ differ by the change
of basis from $|i\rangle\langle j|$ to Pauli operators.

### 1.4 Composition and tensoring

Channels compose sequentially, $(\mathcal E_2\circ\mathcal E_1)(\rho)$ with Kraus set
$\{K^{(2)}_jK^{(1)}_k\}$, and in parallel, $\mathcal E_A\otimes\mathcal E_B$ with Kraus set
$\{K^A_j\otimes K^B_k\}$. Kraus rank multiplies under both operations, which is why the
density-matrix backend applies channels one at a time rather than forming products.

## 2. The Lindblad master equation

### 2.1 Statement

For a Markovian environment the reduced dynamics obeys

$$
\frac{d\rho}{dt} = -\frac{i}{\hbar}[H,\rho] + \sum_k\left(L_k\rho L_k^\dagger - \tfrac12\left\{L_k^\dagger L_k,\rho\right\}\right)
\equiv \mathcal L(\rho),
\tag{2.1}
$$

with jump operators $L_k$ (dimension $\sqrt{\text{rate}}$). The generator $\mathcal L$ is the
most general one producing a CPTP, time-homogeneous semigroup $e^{\mathcal Lt}$
(Gorini–Kossakowski–Sudarshan–Lindblad). Trace is preserved because
$\mathrm{Tr}(L\rho L^\dagger) = \mathrm{Tr}(L^\dagger L\rho)$.

### 2.2 Derivation sketch and assumptions

Start from $H = H_S + H_E + H_{SE}$, $H_{SE} = \hbar\sum_\alpha A_\alpha\otimes B_\alpha$, in the
interaction picture. Second-order perturbation theory in $H_{SE}$ gives an integro-differential
equation for $\rho_S$. Three approximations reduce it to (2.1):

1. **Born** (weak coupling): $\rho_{SE}(t)\approx\rho_S(t)\otimes\rho_E$ with $\rho_E$ stationary.
2. **Markov**: the bath correlation functions $\langle B_\alpha(t)B_\beta(0)\rangle$ decay on a
   time $\tau_E$ much shorter than the system's relaxation time, so the memory integral is
   extended to $\infty$ and $\rho_S(t')\to\rho_S(t)$.
3. **Secular** (rotating-wave): terms oscillating at differences of system Bohr frequencies
   are dropped, valid when those differences exceed the relaxation rates.

The resulting rates are the Fourier transforms of the bath correlation functions evaluated at
the system transition frequencies — Fermi's golden rule. For a qubit coupled through
$A = \sigma_x$ to a bath with noise spectral density $S(\omega)$ (units of rate, two-sided,
$S(\omega) = \int dt\, e^{i\omega t}\langle B(t)B(0)\rangle$):

$$
\gamma_\downarrow = S(\omega_q),\qquad \gamma_\uparrow = S(-\omega_q),\qquad
\frac{\gamma_\uparrow}{\gamma_\downarrow} = e^{-\hbar\omega_q/k_BT}\quad\text{(detailed balance)},
\tag{2.2}
$$

and for coupling through $\sigma_z$ the pure-dephasing rate $\gamma_\varphi = S_z(0)/2$ (with
the appropriate coupling constant absorbed into $S_z$).

**When (2.1) fails.** $1/f$ noise violates Markovianity (its correlation time is not short);
strong drives change the bath-system energy exchange frequencies (the Lindblad operators
must then be derived in the dressed basis); and non-secular terms matter when qubit
frequencies are nearly degenerate. The pulse-level simulator (spec 07 §5) uses (2.1) with
constant rates and documents these limits; $1/f$ dephasing is added as a quasi-static
detuning distribution (§8.3) rather than a Lindblad term.

### 2.3 Thermal photon number

A mode at frequency $\omega$ in equilibrium at temperature $T$ has mean occupation

$$
n_{\rm th}(\omega,T) = \frac{1}{e^{\hbar\omega/k_BT}-1}.
\tag{2.3}
$$

For a $5$ GHz qubit ($\hbar\omega/k_B = 240$ mK):

| $T$ (mK) | $\hbar\omega/k_BT$ | $n_{\rm th}$ | Steady-state $P(|1\rangle) = n_{\rm th}/(2n_{\rm th}+1)$ |
|---:|---:|---:|---:|
| 10 | 24.0 | $3.8\times10^{-11}$ | $3.8\times10^{-11}$ |
| 20 | 12.0 | $6.2\times10^{-6}$ | $6.2\times10^{-6}$ |
| 40 | 6.0 | $2.5\times10^{-3}$ | $2.5\times10^{-3}$ |
| 60 | 4.0 | $1.9\times10^{-2}$ | $1.8\times10^{-2}$ |
| 100 | 2.4 | $1.0\times10^{-1}$ | $8.3\times10^{-2}$ |

Measured residual excited-state populations of $0.1$–$1\%$ on real devices correspond to
effective temperatures of $40$–$70$ mK, above the mixing-chamber temperature, because the
qubit's environment includes thermal photons from the wiring (T07 §7, T08 §6).

## 3. Amplitude damping ($T_1$)

### 3.1 Zero temperature

Jump operator $L = \sqrt{\gamma_1}\,\sigma^-$, $\gamma_1 = 1/T_1$. Solving (2.1) with $H=0$ for
$\rho = \begin{pmatrix}\rho_{00}&\rho_{01}\\\rho_{10}&\rho_{11}\end{pmatrix}$:

$$
\rho_{11}(t) = \rho_{11}(0)\,e^{-t/T_1},\qquad
\rho_{01}(t) = \rho_{01}(0)\,e^{-t/2T_1}.
\tag{3.1}
$$

Kraus operators for duration $t$, with $\gamma = 1-e^{-t/T_1}$:

$$
K_0 = \begin{pmatrix}1&0\\0&\sqrt{1-\gamma}\end{pmatrix},\qquad
K_1 = \begin{pmatrix}0&\sqrt\gamma\\0&0\end{pmatrix}.
\tag{3.2}
$$

Bloch-vector action: $(r_x,r_y,r_z)\to\left(\sqrt{1-\gamma}\,r_x,\ \sqrt{1-\gamma}\,r_y,\ \gamma + (1-\gamma)r_z\right)$;
the Bloch ball contracts toward the north pole $|0\rangle$. The energy relaxation time $T_1$ is
measured by preparing $|1\rangle$ and fitting $P(1)(t) = A e^{-t/T_1} + B$ (spec 22 §4).

### 3.2 Finite temperature (generalized amplitude damping)

With $\gamma_\downarrow = \gamma_0(n_{\rm th}+1)$ and $\gamma_\uparrow = \gamma_0 n_{\rm th}$, jump
operators $L_\downarrow = \sqrt{\gamma_\downarrow}\sigma^-$, $L_\uparrow = \sqrt{\gamma_\uparrow}\sigma^+$:

$$
\frac{1}{T_1} = \gamma_\downarrow+\gamma_\uparrow = \gamma_0(2n_{\rm th}+1),\qquad
\rho_{11}(\infty) = \frac{\gamma_\uparrow}{\gamma_\uparrow+\gamma_\downarrow} = \frac{n_{\rm th}}{2n_{\rm th}+1} \equiv p_{\rm th}.
\tag{3.3}
$$

Kraus operators for duration $t$ ($\gamma = 1-e^{-t/T_1}$):

$$
\begin{aligned}
K_0 &= \sqrt{1-p_{\rm th}}\begin{pmatrix}1&0\\0&\sqrt{1-\gamma}\end{pmatrix}, &
K_1 &= \sqrt{1-p_{\rm th}}\begin{pmatrix}0&\sqrt\gamma\\0&0\end{pmatrix},\\
K_2 &= \sqrt{p_{\rm th}}\begin{pmatrix}\sqrt{1-\gamma}&0\\0&1\end{pmatrix}, &
K_3 &= \sqrt{p_{\rm th}}\begin{pmatrix}0&0\\\sqrt\gamma&0\end{pmatrix}.
\end{aligned}
\tag{3.4}
$$

The fixed point is $\mathrm{diag}(1-p_{\rm th},\,p_{\rm th})$; coherences still decay as
$e^{-t/2T_1}$. The calibration file stores $T_1$ and $p_{\rm th}$ (or equivalently an
effective temperature); $n_{\rm th}$ follows from (3.3).

## 4. Pure dephasing and $T_2$

### 4.1 Pure dephasing ($T_\varphi$)

Jump operator $L = \sqrt{\gamma_\varphi/2}\,Z$ gives

$$
\dot\rho_{01} = -\gamma_\varphi\,\rho_{01},\qquad \rho_{01}(t) = \rho_{01}(0)\,e^{-t/T_\varphi},\qquad
\gamma_\varphi = 1/T_\varphi,
\tag{4.1}
$$

with populations untouched. Kraus operators (phase damping) for duration $t$, with
$\lambda = 1-e^{-2t/T_\varphi}$:

$$
K_0 = \begin{pmatrix}1&0\\0&\sqrt{1-\lambda}\end{pmatrix},\qquad
K_1 = \begin{pmatrix}0&0\\0&\sqrt\lambda\end{pmatrix};
\quad\text{equivalently}\quad
\mathcal E(\rho) = (1-p)\rho + pZ\rho Z,\ \ p = \tfrac12\left(1-e^{-t/T_\varphi}\right).
\tag{4.2}
$$

Bloch action: $(r_x,r_y,r_z)\to(e^{-t/T_\varphi}r_x,\ e^{-t/T_\varphi}r_y,\ r_z)$.

### 4.2 Bloch equations and the $T_2$ relation

With both processes, $H = -\tfrac12\hbar\Delta Z$ (detuning $\Delta$ in the rotating frame),
the Bloch vector obeys

$$
\dot r_x = -\Delta r_y - r_x/T_2,\qquad
\dot r_y = \Delta r_x - r_y/T_2,\qquad
\dot r_z = -(r_z - r_z^{\rm eq})/T_1,
\tag{4.3}
$$

where the transverse rate collects the coherence decay of (3.1) and (4.1):

$$
\frac{1}{T_2} = \frac{1}{2T_1} + \frac{1}{T_\varphi}
\qquad\Longrightarrow\qquad T_2\le 2T_1 .
\tag{4.4}
$$

Derivation: $\rho_{01}$ is multiplied by $e^{-t/2T_1}$ from amplitude damping (coherence is
the geometric mean of the two populations' survival amplitudes) and by $e^{-t/T_\varphi}$ from
dephasing; the exponents add. A calibration file with $T_2 > 2T_1$ is rejected as inconsistent
(spec 08 §3).

### 4.3 The thermal-relaxation channel used per gate

For an idle period or a gate of duration $\tau$, the simulator applies amplitude damping (3.4)
followed by phase damping (4.2) with

$$
\gamma = 1-e^{-\tau/T_1},\qquad
\lambda = 1-e^{-2\tau/T_\varphi},\qquad
\frac{1}{T_\varphi} = \frac{1}{T_2}-\frac{1}{2T_1}.
\tag{4.5}
$$

Its Bloch map has diagonal contraction $(e^{-\tau/T_2}, e^{-\tau/T_2}, e^{-\tau/T_1})$ plus a
$z$-shift, and its average gate fidelity (T10 (1.3)) is

$$
F_{\rm avg}^{\rm therm}(\tau) = \frac12 + \frac{2e^{-\tau/T_2} + e^{-\tau/T_1}}{6}
\ \approx\ 1 - \frac{\tau}{6}\left(\frac{1}{T_1}+\frac{2}{T_2}\right)\quad(\tau\ll T_1,T_2).
\tag{4.6}
$$

Example: $\tau = 35$ ns, $T_1 = 150$ μs, $T_2 = 100$ μs gives
$1 - F = 1.6\times10^{-4}$, which is the coherence-limited floor for a single-qubit gate on that
qubit; a calibrated infidelity below it is inconsistent.

## 5. Depolarizing and Pauli channels

### 5.1 Definitions

Single qubit, depolarizing probability $p$:

$$
\mathcal E_{\rm dep}(\rho) = (1-p)\rho + p\,\frac{I}{2}
= \left(1-\tfrac{3p}{4}\right)\rho + \tfrac p4\left(X\rho X + Y\rho Y + Z\rho Z\right).
\tag{5.1}
$$

The Bloch vector contracts uniformly, $\vec r\to(1-p)\vec r$. On $d$ dimensions
($d = 2^n$ for an $n$-qubit gate),

$$
\mathcal E_{\rm dep}^{(d)}(\rho) = (1-p)\rho + p\,\frac{I}{d}
= \left(1-p\,\tfrac{d^2-1}{d^2}\right)\rho + \frac{p}{d^2}\sum_{P\ne I}P\rho P^\dagger,
\tag{5.2}
$$

summing over the $d^2-1$ non-identity Pauli strings. **Pauli channel:**
$\mathcal E(\rho) = \sum_P p_P\,P\rho P^\dagger$ with $\sum_P p_P = 1$; bit flip
($p_X = p$), phase flip ($p_Z = p$), bit-phase flip ($p_Y = p$) are the one-term cases.

### 5.2 Relation to fidelity

For the depolarizing channel (5.2), with $F_{\rm avg}$ the average gate fidelity of T10 §1,

$$
F_{\rm avg} = 1 - p\,\frac{d-1}{d},\qquad
F_{\rm pro} = 1 - p\,\frac{d^2-1}{d^2},\qquad
F_{\rm avg} = \frac{d\,F_{\rm pro}+1}{d+1}.
\tag{5.3}
$$

Single qubit: $1-F_{\rm avg} = p/2$; two qubits: $1-F_{\rm avg} = 3p/4$. Derivation is in
T10 §1.3; these are the formulas spec 08 uses to convert a calibrated gate error into $p$.

### 5.3 Pauli twirling

Conjugating a channel by a random Pauli before and after,
$\mathcal E^{\rm tw}(\rho) = \frac{1}{4^n}\sum_P P^\dagger\,\mathcal E(P\rho P^\dagger)\,P$,
produces a Pauli channel with the same average fidelity and the same diagonal of the
$\chi$ matrix. Twirling over the full Clifford group produces a depolarizing channel. This is
why randomized benchmarking measures a depolarizing parameter (T10 §2) and why the
stabilizer backend, which can only simulate Pauli errors, is a valid model of a twirled
device (spec 07 §4).

## 6. Building the per-gate noise model from calibration

For each gate instance of duration $\tau$ on qubits $Q$ with calibrated average fidelity
$F_g$ the simulator composes, in this order (spec 08 §4):

1. The ideal unitary $U_g$.
2. Thermal relaxation (4.5) on each qubit in $Q$ for duration $\tau$, and on every *other*
   qubit for the same $\tau$ if it is idle in that time slot (the scheduler provides idle
   intervals, spec 14 §7).
3. A depolarizing channel on $Q$ with

$$
p_{\rm dep} = \frac{d}{d-1}\,\max\!\left(0,\ (1-F_g) - (1-F_{\rm avg}^{\rm therm}(\tau))\right),
\tag{6.1}
$$

so that the total average infidelity equals the calibrated $1-F_g$ to first order, and the
coherence-limited part is not double counted.

4. Optional coherent terms: over-rotation $R_{\hat n}(\epsilon)$, ZZ phase (§9.1), and leakage
   (§9.2), each parametrised in the calibration file.

Long idle periods are split into segments no longer than $\min(T_1,T_2)/100$ so that
(4.5) is applied with the correct exponential rather than a linearisation.

## 7. Quantum trajectories (Monte-Carlo wave function)

The Lindblad equation (2.1) is equivalent to an ensemble average over stochastic pure-state
trajectories, which costs $O(2^n)$ memory per trajectory instead of $O(4^n)$ for $\rho$.
Algorithm for one time step $\delta t$ (first-order form):

1. Compute the jump probabilities $\delta p_k = \delta t\,\langle\psi|L_k^\dagger L_k|\psi\rangle$,
   $\delta p = \sum_k\delta p_k$; require $\delta p\ll1$ (the integrator halves $\delta t$ if
   $\delta p > 0.1$).
2. Draw $r\in[0,1)$ uniformly.
3. If $r<\delta p$: **jump** — choose $k$ with probability $\delta p_k/\delta p$ and set
   $|\psi\rangle\to L_k|\psi\rangle/\|L_k|\psi\rangle\|$.
4. Otherwise: **no jump** — evolve with the non-Hermitian effective Hamiltonian

$$
H_{\rm eff} = H - \frac{i\hbar}{2}\sum_kL_k^\dagger L_k,\qquad
|\psi\rangle\to\frac{e^{-iH_{\rm eff}\delta t/\hbar}|\psi\rangle}{\sqrt{1-\delta p}}.
\tag{7.1}
$$

Averaging $|\psi\rangle\langle\psi|$ over trajectories reproduces (2.1) to first order in
$\delta t$: the no-jump branch contributes
$(1-\delta p)\,\frac{(1 - iH_{\rm eff}\delta t/\hbar)\rho(1+iH_{\rm eff}^\dagger\delta t/\hbar)}{1-\delta p}$
and the jump branches $\delta t\sum_k L_k\rho L_k^\dagger$; the anticommutator term in (2.1) is
exactly the non-Hermitian part of $H_{\rm eff}$. The production integrator uses the
waiting-time form: evolve under $H_{\rm eff}$ (with an adaptive integrator, T11 §5) until
$\langle\psi|\psi\rangle$ falls to a pre-drawn $r$, then jump. With $N_{\rm traj}$ trajectories an
expectation value has statistical error $\propto1/\sqrt{N_{\rm traj}}$; the simulator reports
it as `Statistical`.

## 8. Measuring coherence: Ramsey, echo, and noise spectra

### 8.1 Ramsey ($T_2^*$)

Sequence: $\text{sx}$ — wait $t$ — $\text{sx}$ (with a deliberate detuning $\Delta$ or a
phase ramp on the second pulse) — measure. Under (4.3) with $T_1\to\infty$ the excited-state
probability is

$$
P_1(t) = \frac12\left[1 + e^{-t/T_2^*}\cos(\Delta t + \phi_0)\right],
\tag{8.1}
$$

a decaying fringe whose frequency gives the detuning (used to calibrate the qubit frequency)
and whose envelope gives $T_2^*$. The asterisk marks that quasi-static (slow) frequency
fluctuations are included: they average out over shots and shorten the observed decay.

### 8.2 Hahn echo ($T_{2E}$) and CPMG

Inserting an $X$ pulse at $t/2$ inverts the accumulated phase, so any detuning constant over
$t$ cancels; the envelope then decays with $T_{2E}\ge T_2^*$, sensitive only to noise at
frequencies $\gtrsim1/t$. A CPMG train of $N$ $\pi$-pulses at spacing $t/N$ pushes the filter
function's pass-band to $\sim N/(2t)$, which is used to reconstruct the noise spectrum
$S_z(\omega)$ from the decay versus $N$ (dynamical-decoupling noise spectroscopy).

### 8.3 Gaussian versus exponential decay and $1/f$ noise

If the qubit frequency is offset by a random but shot-to-shot constant detuning
$\delta\sim\mathcal N(0,\sigma^2)$, the Ramsey signal averages to

$$
\left\langle\cos(\delta t)\right\rangle = e^{-\sigma^2t^2/2}
\qquad\Longrightarrow\qquad
P_1(t) = \tfrac12\left[1 + e^{-t/T_1'}\,e^{-(t/T_2^*)^2}\cos(\Delta t)\right],\quad T_2^* = \frac{\sqrt2}{\sigma},
\tag{8.2}
$$

Gaussian rather than exponential. This is the signature of low-frequency ($1/f$) flux and
charge noise in transmons and of magnetic-field drift in ions. The calibration file records
a `ramsey_shape` of `exponential`, `gaussian`, or a mixed exponent; the simulator draws
$\delta$ once per shot from the stated distribution and applies it as a detuning term in the
Hamiltonian, which is exact for quasi-static noise and outside the Lindblad model (§2.2).
Frequency-dependent $1/f$ spectra $S(\omega) = A/|\omega|$ are handled by summing a quasi-static
component (for $\omega<1/t_{\rm exp}$) and a Markovian remainder.

## 9. Coherent errors

### 9.1 ZZ crosstalk

Two coupled transmons acquire a state-dependent frequency shift (T05 §7). In the doubly
rotating frame the always-on interaction is

$$
H_{ZZ} = \frac{\hbar\zeta}{4}\,Z\otimes Z,\qquad
U_{ZZ}(t) = e^{-i\zeta t\,ZZ/4},\qquad
\zeta = \frac{E_{11}-E_{10}-E_{01}+E_{00}}{\hbar},
\tag{9.1}
$$

so qubit $a$'s frequency is shifted by $\pm\zeta/2$ depending on the state of qubit $b$.
Typical $\zeta/2\pi$: $50$–$300$ kHz for fixed-coupling devices, $<10$ kHz with tunable
couplers. The simulator applies (9.1) on every coupled pair during idle intervals and during
single-qubit gates; it is coherent, so it can be echoed away, which the example programs
demonstrate.

### 9.2 Over-rotation and axis error

A calibrated pulse implements $R_{\hat n'}(\theta+\epsilon)$ with $\hat n'$ tilted by a small
angle from $\hat n$. The per-gate infidelity is $\approx\epsilon^2/4 + (\text{tilt})^2\sin^2(\theta/2)$
to leading order (T10 §1). Coherent errors add in amplitude over repeated gates
(error $\propto N\epsilon$) whereas incoherent errors add in probability ($\propto N\epsilon^2$),
which is why randomized benchmarking underestimates their impact on structured circuits.

### 9.3 Leakage

A transmon is a multi-level system; a fast pulse populates $|2\rangle$ (T05 §9). Leakage is
modelled either exactly (Lindblad backend with $d=3$ per qubit) or, in the gate-level
backends, as a leakage channel with rate $L_1$ per gate into a third level that is then
treated as a classical marker: a leaked qubit reads out as $|1\rangle$ with the calibrated
probability and returns to the qubit subspace with seepage rate $L_2$ per gate. The
stabilizer backend cannot represent leakage and reports it as unsupported.

## 10. State preparation and measurement (SPAM) errors

### 10.1 Readout confusion matrix

Readout assigns a classical label to the projected state with errors. For one qubit,

$$
M = \begin{pmatrix}1-\epsilon_0 & \epsilon_1\\ \epsilon_0 & 1-\epsilon_1\end{pmatrix},\qquad
M_{ij} = P(\text{read } i \mid \text{prepared } j),\qquad
\vec p_{\rm meas} = M\,\vec p_{\rm true}.
\tag{10.1}
$$

$\epsilon_0 = P(1|0)$, $\epsilon_1 = P(0|1)$; typically $\epsilon_1>\epsilon_0$ because $T_1$
decay during the readout pulse converts $|1\rangle$ to $|0\rangle$, with contribution
$\approx t_{\rm ro}/2T_1$. The assignment fidelity is $F_{\rm ro} = 1-(\epsilon_0+\epsilon_1)/2$.
For $n$ qubits with independent readout errors, $M = M_{n-1}\otimes\cdots\otimes M_0$
(little-endian); correlated readout errors are stored as a full $2^n\times2^n$ matrix for
$n\le6$ or as pairwise corrections.

### 10.2 Readout error mitigation

Given measured counts $\vec c$ over $N$ shots, the unbiased estimate is
$\vec p_{\rm true} = M^{-1}\vec c/N$, which can have negative entries because of shot noise;
the standard remedy is constrained least squares, $\min\|M\vec p - \vec c/N\|_2$ subject to
$p_i\ge0$, $\sum p_i = 1$. The variance of the mitigated estimate is amplified by
$\|M^{-1}\|^2$, roughly $(1-\epsilon_0-\epsilon_1)^{-2n}$ for $n$ qubits, which bounds the
useful register size for this method to $n\lesssim 10$ at $1\%$ readout error. The analysis
panel (spec 22 §6) reports both the raw and mitigated distributions with their errors.

### 10.3 State-preparation error

Reset leaves the qubit in $|1\rangle$ with probability $p_{\rm prep}$ (thermal population
$p_{\rm th}$ of (3.3), plus reset infidelity for active reset). Modelled as a bit-flip channel
with probability $p_{\rm prep}$ applied at the start of every shot.

### 10.4 Measurement-induced dephasing

During dispersive readout the resonator photons carry which-state information, dephasing any
superposition of the measured qubit at rate $\Gamma_m = 8\chi^2\bar n/\kappa$ for $\chi\ll\kappa$, where
$2\chi$ is the resonator frequency difference between qubit states, $\bar n$ the mean photon
number and $\kappa$ the resonator linewidth (T05 §6) — this is
the mechanism of projection. Mid-circuit measurement of qubit $a$ also dephases a neighbouring
qubit $b$ through the residual dispersive shift of $b$ on $a$'s resonator; the calibration file
lists this as a per-readout dephasing probability on neighbours.

## 11. Summary of channels implemented

| Channel | Parameters | Kraus rank | Bloch action | Class |
|---------|-----------|-----------:|--------------|-------|
| Amplitude damping | $T_1$, $\tau$ | 2 | contract $x,y$ by $e^{-\tau/2T_1}$, $z\to$ pole | Exact |
| Generalized amplitude damping | $T_1$, $p_{\rm th}$, $\tau$ | 4 | as above toward $r_z = 1-2p_{\rm th}$ | Exact |
| Phase damping | $T_\varphi$, $\tau$ | 2 | contract $x,y$ by $e^{-\tau/T_\varphi}$ | Exact |
| Thermal relaxation | $T_1,T_2,p_{\rm th},\tau$ | ≤ 8 (composed) | (4.5) | Exact |
| Depolarizing | $p$ (per gate, from $F_g$) | 4 or 16 | uniform contraction | Exact |
| Pauli | $\{p_P\}$ | up to $4^n$ | axis-wise contraction | Exact |
| Quasi-static detuning | $\sigma$, shape | — (per-shot unitary) | rotation about $z$ | Statistical |
| ZZ crosstalk | $\zeta$ per edge | — (unitary) | conditional $z$ rotation | Exact |
| Over-rotation / tilt | $\epsilon$, tilt | — (unitary) | rotation error | Exact |
| Leakage | $L_1,L_2$ | 3-level | outside Bloch ball | Numerical / Model |
| Readout confusion | $\epsilon_0,\epsilon_1$ or full $M$ | classical | — | Model |
| State-preparation | $p_{\rm prep}$ | 2 | bit flip at $t=0$ | Model |

## Where this is used

| Result | Used by |
|--------|---------|
| (1.2)–(1.3) Kraus and Choi | Spec 08 §2 channel loading and CPTP checks; spec 22 process-tomography output |
| (2.1) Lindblad equation | Spec 07 §5 Lindblad backend; T11 §5 integrator |
| (2.2)–(2.3) rates and $n_{\rm th}$ | Spec 09 calibration consistency, spec 11 §6 wiring-temperature→$p_{\rm th}$, spec 12 thermometry |
| (3.2)–(3.4) amplitude damping | Spec 08 §3, spec 22 $T_1$ fit model |
| (4.2)–(4.6) dephasing and $T_2$ | Spec 08 §3 validation ($T_2\le2T_1$), spec 15 coherence-limited fidelity floor |
| (5.1)–(5.3) depolarizing ↔ fidelity | Spec 08 §4, spec 15 fidelity estimator, spec 25 RB oracle |
| §5.3 twirling | Spec 07 §4 stabilizer-backend noise, spec 16 QEC error models |
| (6.1) per-gate composition | Spec 08 §4 `NoiseModel::fromCalibration` |
| §7 trajectories | Spec 07 §5 `LindbladBackend::trajectories`, spec 24 memory budget |
| (8.1)–(8.2) Ramsey/echo | Spec 12 calibration experiments, spec 22 fit models |
| (9.1) ZZ | Spec 08 §5, spec 09 edge parameters, spec 10 echo sequences |
| §9.3 leakage | Spec 07 §5 three-level transmon, spec 08 §6 |
| (10.1)–(10.2) readout matrix | Spec 08 §7, spec 12 digitizer discriminator, spec 22 §6 mitigation |
| §11 table | Spec 08 §1 channel catalog |
