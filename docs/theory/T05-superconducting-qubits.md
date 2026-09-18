# T05 — Superconducting Qubits

This document derives the transmon and its circuit-QED environment from the quantized LC
oscillator, states the models the simulator integrates (`hw::TransmonHamiltonian`,
`hw::CouplerModel`, `pulse` gate calibrations), and gives the parameter ranges of 2023–2025
devices that populate the `Model` fidelity class. Sign conventions used throughout:
$\Delta \equiv \omega_q - \omega_r$ for qubit–resonator detuning,
$\Delta_{ct} \equiv \omega_c - \omega_t$ for control–target detuning, and the transmon
anharmonicity $\alpha < 0$.

## 1. The quantized LC oscillator

A lumped LC circuit with capacitance $C$ and inductance $L$ has node flux $\Phi(t)=\int_{-\infty}^t V(t')\,dt'$ and node charge $Q = C\dot\Phi$. Its Lagrangian is $\mathcal L = \tfrac12 C\dot\Phi^2 - \Phi^2/2L$, the conjugate momentum is $Q = \partial\mathcal L/\partial\dot\Phi$, and the Hamiltonian is

$$
H = \frac{Q^2}{2C} + \frac{\Phi^2}{2L}. \tag{1.1}
$$

Quantization promotes $\Phi, Q$ to operators with

$$
[\hat\Phi, \hat Q] = i\hbar. \tag{1.2}
$$

Defining the characteristic impedance $Z_0 = \sqrt{L/C}$ and ladder operators

$$
\hat\Phi = \sqrt{\frac{\hbar Z_0}{2}}\,(a + a^\dagger), \qquad
\hat Q = i\sqrt{\frac{\hbar}{2 Z_0}}\,(a^\dagger - a), \tag{1.3}
$$

gives $H = \hbar\omega_r (a^\dagger a + \tfrac12)$ with

$$
\omega_r = \frac{1}{\sqrt{LC}}. \tag{1.4}
$$

The zero-point fluctuations are $\Phi_{\mathrm{zpf}} = \sqrt{\hbar Z_0/2}$ and $Q_{\mathrm{zpf}} = \sqrt{\hbar/2Z_0}$. Because the spectrum is equally spaced, an LC oscillator cannot be a qubit: a drive at $\omega_r$ climbs the whole ladder. A nonlinear, dissipationless inductance is required.

## 2. The Josephson junction

A tunnel junction between two superconductors with phase difference $\varphi$ obeys the Josephson relations

$$
I = I_c \sin\varphi, \qquad V = \frac{\Phi_0}{2\pi}\,\dot\varphi, \tag{2.1}
$$

where $I_c$ is the critical current and $\Phi_0 = h/2e = 2.067\,833\,848\times10^{-15}$ Wb is the flux quantum. The energy stored is $\int IV\,dt = -E_J\cos\varphi$ with the Josephson energy

$$
E_J = \frac{\Phi_0 I_c}{2\pi}. \tag{2.2}
$$

Differentiating (2.1), $\dot I = I_c\cos\varphi\,\dot\varphi = I_c\cos\varphi\,(2\pi/\Phi_0)V$, so the junction behaves as a nonlinear inductor

$$
L_J(\varphi) = \frac{\Phi_0}{2\pi I_c\cos\varphi} = \frac{L_{J0}}{\cos\varphi}, \qquad L_{J0} = \frac{\Phi_0}{2\pi I_c} = \left(\frac{\Phi_0}{2\pi}\right)^2\frac{1}{E_J}. \tag{2.3}
$$

Numbers: $I_c = 30$ nA gives $E_J/h = \Phi_0 I_c/(2\pi h) = 14.9$ GHz and $L_{J0} = 11.0$ nH. Room-temperature junction resistance $R_n$ predicts $I_c$ by the Ambegaokar–Baratoff relation $I_c R_n = \pi\Delta_{\mathrm{sc}}/2e$ with the aluminium gap $\Delta_{\mathrm{sc}} \approx 180\ \mu$eV, i.e. $I_c R_n \approx 283\ \mu$V; a target $E_J/h=15$ GHz needs $R_n \approx 9.4$ k$\Omega$. This is how the `Model` device files derive $E_J$ from a fabrication spec.

## 3. Cooper-pair box and the transmon

A junction shunted by total capacitance $C_\Sigma$ (junction + shunt + gate capacitances) with an offset charge $n_g$ (in units of $2e$) induced by a gate voltage has the Hamiltonian

$$
H = 4E_C(\hat n - n_g)^2 - E_J\cos\hat\varphi, \qquad E_C = \frac{e^2}{2C_\Sigma}, \tag{3.1}
$$

where $\hat n$ counts Cooper pairs transferred across the junction and $[\hat\varphi, \hat n] = i$. The factor 4 arises because the charge is $2e\hat n$ and $(2e)^2/2C_\Sigma = 4E_C$.

### 3.1 Charge-basis matrix (what the simulator diagonalizes)

In the charge basis $\{|n\rangle\}$, $\cos\hat\varphi = \tfrac12\sum_n (|n\rangle\langle n+1| + |n+1\rangle\langle n|)$, so $H$ is tridiagonal:

$$
H_{nn} = 4E_C (n - n_g)^2, \qquad H_{n,n\pm1} = -\frac{E_J}{2}. \tag{3.2}
$$

Truncating to $n \in [-N, N]$ with $N = 15$ reproduces the lowest 4 eigenvalues of a transmon with $E_J/E_C \le 100$ to better than $10^{-9}$ relative error (`25 §3` oracle). The simulator (`hw::TransmonHamiltonian::eigen`) diagonalizes (3.2) with a symmetric tridiagonal QL solver (`06 §4`) at every flux point; this is the `Numerical`-class source of $\omega_{01}$, $\alpha$, and charge dispersion. The charge and phase matrix elements in the eigenbasis, $\langle i|\hat n|j\rangle$, feed the drive and coupling terms of §6–§8.

### 3.2 Transmon limit

For $E_J/E_C \gg 1$ the phase is localized near $\varphi = 0$, and expanding $\cos\varphi \approx 1 - \varphi^2/2 + \varphi^4/24$ gives a weakly anharmonic oscillator. Perturbation theory in the quartic term yields the eigenenergies

$$
E_m \simeq -E_J + \sqrt{8E_JE_C}\left(m + \tfrac12\right) - \frac{E_C}{12}\left(6m^2 + 6m + 3\right). \tag{3.3}
$$

Hence

$$
\hbar\omega_{01} \simeq \sqrt{8E_JE_C} - E_C, \qquad
\hbar\alpha \equiv E_{12} - E_{01} \simeq -E_C, \tag{3.4}
$$

where $E_{ij} = E_j - E_i$. The plasma frequency $\omega_p = \sqrt{8E_JE_C}/\hbar$ is the frequency of the linearized LC formed by $L_{J0}$ and $C_\Sigma$; the anharmonicity is set by the charging energy alone.

The residual sensitivity of level $m$ to the offset charge (the *charge dispersion*) is exponentially suppressed:

$$
\epsilon_m \simeq (-1)^m E_C\,\frac{2^{4m+5}}{m!}\sqrt{\frac{2}{\pi}}\left(\frac{E_J}{2E_C}\right)^{\frac m2 + \frac34} e^{-\sqrt{8E_J/E_C}}, \tag{3.5}
$$

where $\epsilon_m$ is the peak-to-peak variation of $E_m$ as $n_g$ sweeps one period. This is the exchange that buys insensitivity to charge noise at the price of a small anharmonicity. Charge dispersion of the $|1\rangle$ level appears in the simulator as a slow frequency wander of amplitude $\epsilon_1/h$ (quasiparticle parity switching, `08 §5`).

### 3.3 Worked example

$E_J/h = 15.0$ GHz, $E_C/h = 0.300$ GHz, so $E_J/E_C = 50$:

| Quantity | Formula | Value |
|----------|---------|-------|
| $\omega_p/2\pi$ | $\sqrt{8E_JE_C}/h$ | $\sqrt{36} = 6.000$ GHz |
| $\omega_{01}/2\pi$ | (3.4) | $5.700$ GHz |
| $\alpha/2\pi$ | (3.4) | $-300$ MHz |
| $\omega_{12}/2\pi$ | $\omega_{01} + \alpha$ | $5.400$ GHz |
| $\epsilon_1/h$ | (3.5), $m=1$ | $-0.3\ \mathrm{GHz}\times 512\times0.798\times 55.9\times e^{-20} \approx -14$ kHz |
| $C_\Sigma$ | $e^2/2E_C$ | $64.4$ fF |
| $I_c$ | $2\pi E_J/\Phi_0$ | $30.2$ nA |

Exact diagonalization of (3.2) gives $\omega_{01}/2\pi = 5.714$ GHz and $\alpha/2\pi = -318$ MHz; the difference from (3.4) is the next order in $E_C/E_J$. The simulator always uses the exact values and displays (3.4) only as the theory overlay.

### 3.4 Duffing (Kerr) approximation

Keeping $d$ levels of the transmon as a Kerr oscillator,

$$
H_{\mathrm{Kerr}} = \hbar\omega\, b^\dagger b + \frac{\hbar\alpha}{2}\, b^\dagger b^\dagger b\, b, \tag{3.6}
$$

reproduces $E_m = \hbar\omega m + \hbar\alpha\, m(m-1)/2$, which matches (3.3) through the quadratic term. The Lindblad backend (`07 §6`) uses (3.6) with $d = 3$ by default ($d = 4$ for leakage studies) and takes $\omega, \alpha$ from the exact diagonalization, not from (3.4). Truncation error: with $d=3$ the population leaking to $|3\rangle$ during a 20 ns DRAG $\pi$ pulse is below $10^{-5}$ and is neglected; the `pulse` validator warns when a schedule's spectral content within $|\alpha|$ of $\omega_{23}$ exceeds $-40$ dB of its peak.

## 4. Flux-tunable transmons

Replacing the junction by a SQUID loop of two junctions $E_{J1}, E_{J2}$ threaded by flux $\Phi$ gives an effective Josephson energy

$$
E_J(\Phi) = E_{J\Sigma}\left|\cos\left(\frac{\pi\Phi}{\Phi_0}\right)\right|\sqrt{1 + d^2\tan^2\left(\frac{\pi\Phi}{\Phi_0}\right)},\qquad
E_{J\Sigma} = E_{J1} + E_{J2},\quad d = \frac{E_{J2} - E_{J1}}{E_{J\Sigma}}. \tag{4.1}
$$

With (3.4) the qubit frequency follows $\omega_{01}(\Phi) \simeq \left(\sqrt{8E_J(\Phi)E_C} - E_C\right)/\hbar$. For a symmetric SQUID ($d=0$),

$$
\frac{\partial\omega_{01}}{\partial\Phi} = -\frac{\pi}{\Phi_0}\,\frac{\sqrt{8E_{J\Sigma}E_C}}{2\hbar}\,\frac{\sin(\pi\Phi/\Phi_0)}{\sqrt{|\cos(\pi\Phi/\Phi_0)|}}\,\mathrm{sgn}\cos(\pi\Phi/\Phi_0). \tag{4.2}
$$

The derivative vanishes at $\Phi = 0$ (the *upper sweet spot*) and, for $d \ne 0$, at $\Phi = \Phi_0/2$ (the *lower sweet spot*, frequency $\omega_{01}(\Phi_0/2)$ set by $d\,E_{J\Sigma}$). Away from a sweet spot, flux noise with spectral density $S_\Phi(f) = A_\Phi^2/f$ ($A_\Phi \approx 1$–$5\ \mu\Phi_0/\sqrt{\mathrm{Hz}}$ at 1 Hz for typical loops) dephases the qubit at a rate (Gaussian-decay $1/e$ time)

$$
\frac{1}{T_\varphi^{1/f}} \simeq \left|\frac{\partial\omega_{01}}{\partial\Phi}\right| A_\Phi\sqrt{\ln\frac{1}{2\pi f_{\mathrm{ir}} t}}\,, \tag{4.3}
$$

with $f_{\mathrm{ir}}$ the infrared cutoff set by the measurement duration (the simulator uses $f_{\mathrm{ir}} = 1$ Hz and the log factor $\approx 4$). Numbers: $\omega_{01}/2\pi = 5$ GHz tuned 500 MHz below the sweet spot on a symmetric SQUID gives $|\partial\omega_{01}/\partial\Phi|/2\pi \approx 3.1$ GHz/$\Phi_0$; with $A_\Phi = 2\ \mu\Phi_0$ that is $T_\varphi \approx 6\ \mu$s — the reason tunable qubits are parked at sweet spots and moved only for gates. The `Model` noise builder (`08 §4`) uses (4.3) to derive $T_\varphi$ from the flux operating point.

A flux line with mutual inductance $M$ to the loop sets $\Phi = M I_{\mathrm{flux}} + \Phi_{\mathrm{offset}}$; typical $M = 1$–$3$ pH so that $\Phi_0$ corresponds to $0.7$–$2$ mA (`T07 §11`).

## 5. Coupling between transmons

### 5.1 Direct capacitive coupling

Two transmons with total capacitances $C_{\Sigma1}, C_{\Sigma2}$ joined by a coupling capacitor $C_g \ll C_{\Sigma i}$ acquire the charge–charge interaction $H_{\mathrm{int}} = 4e^2\,\frac{C_g}{C_{\Sigma1}C_{\Sigma2}}\,\hat n_1 \hat n_2$. In the transmon eigenbasis $\hat n_i \simeq i\,n_{\mathrm{zpf},i}(b_i^\dagger - b_i)$ with $n_{\mathrm{zpf}} = (E_J/32E_C)^{1/4}$, and dropping counter-rotating terms,

$$
H_{\mathrm{int}} \simeq \hbar g\,(b_1^\dagger b_2 + b_1 b_2^\dagger), \qquad
g = \frac{1}{2}\,\frac{C_g}{\sqrt{C_{\Sigma1}C_{\Sigma2}}}\,\sqrt{\omega_1\omega_2}. \tag{5.1}
$$

Numbers: $C_g = 0.07$ fF, $C_\Sigma = 64$ fF, $\omega/2\pi = 5.7$ GHz gives $g/2\pi = 3.1$ MHz — the fixed-coupling regime used for cross-resonance devices. In the frame rotating at each qubit's own frequency the exchange term oscillates at the detuning $\Delta_{12} = \omega_1 - \omega_2$; for $|\Delta_{12}| \gg g$ it produces only the dispersive shifts of §5.3 and the ZZ term of §5.4, and the qubits are effectively uncoupled until driven (§8) or brought into resonance (§9).

### 5.2 Tunable coupler

Inserting a third transmon (the coupler, frequency $\omega_c$) between qubits 1 and 2 with couplings $g_{1c}, g_{2c}$ to the coupler and a residual direct coupling $g_{12}$ gives, after eliminating the coupler to second order in $g_{ic}/\Delta_i$ with $\Delta_i = \omega_i - \omega_c$,

$$
\tilde g = g_{12} + \frac{g_{1c}\,g_{2c}}{2}\left(\frac{1}{\Delta_1} + \frac{1}{\Delta_2}\right). \tag{5.2}
$$

With the coupler above both qubits ($\Delta_i < 0$) the second term is negative and (5.2) crosses zero at a coupler frequency $\omega_c^{\mathrm{off}}$: the *off point*. Tuning the coupler downward toward the qubits turns $\tilde g$ on to $10$–$40$ MHz. Numbers: $g_{1c} = g_{2c} = 100$ MHz, $g_{12} = 5$ MHz, qubits at 5.0 and 5.1 GHz; the off point satisfies $\frac{g_{1c}g_{2c}}{2}(1/\Delta_1 + 1/\Delta_2) = -5$ MHz, i.e. $\omega_c^{\mathrm{off}}/2\pi \approx 7.05$ GHz. The simulator's coupler model (`hw::CouplerModel`) implements the full three-body Kerr Hamiltonian and uses (5.2) only for the theory overlay and for the fast `Model` estimate of gate rates.

### 5.3 Dispersive shifts between qubits

For $|\Delta_{12}| \gg g$ each qubit's frequency shifts by $\pm g^2/\Delta_{12}$ (the sign depends on which is higher). Calibration files store the *dressed* frequencies; the simulator never adds this shift twice.

### 5.4 The ZZ interaction

Second-order perturbation theory in $g$ with both transmons kept as Kerr oscillators gives the energy of $|11\rangle$ shifted by its couplings to $|20\rangle$ (matrix element $\sqrt2 g$, detuning $-\Delta_{12} - \alpha_1$) and $|02\rangle$ ($\sqrt2 g$, detuning $\Delta_{12} - \alpha_2$), while the shifts of $|01\rangle$ and $|10\rangle$ cancel. The static ZZ coefficient $\zeta \equiv (E_{11} - E_{01} - E_{10} + E_{00})/\hbar$, entering the two-qubit Hamiltonian as $H_{ZZ} = \frac{\hbar\zeta}{4}\,Z\otimes Z$ (the same definition and normalization as `T04 §9.1`), is

$$
\zeta = 2g^2\left(\frac{1}{\Delta_{12} - \alpha_2} - \frac{1}{\Delta_{12} + \alpha_1}\right)
     = \frac{2g^2\,(\alpha_1 + \alpha_2)}{(\Delta_{12} + \alpha_1)(\Delta_{12} - \alpha_2)}. \tag{5.3}
$$

Numbers: $g/2\pi = 3$ MHz, $\Delta_{12}/2\pi = 150$ MHz, $\alpha_{1,2}/2\pi = -330$ MHz gives $\zeta/2\pi = 137$ kHz, an always-on conditional phase $\zeta t$ ($\zeta$ in rad/s) that accumulates $0.09$ rad during a 100 ns idle. With a tunable coupler, (5.3) is evaluated with $\tilde g$ and the coupler-induced corrections; at the off point $\zeta$ can be tuned through zero, which is why tunable-coupler devices reach two-qubit errors below $10^{-3}$. The noise model applies $\zeta$ as a coherent $ZZ$ rotation on every idle edge (`08 §6`); the compiler's scheduler can insert echo sequences to cancel it (`14 §8`).

## 6. Circuit QED: qubit in a resonator

### 6.1 Jaynes–Cummings model

A two-level qubit at $\omega_q$ coupled with strength $g$ to a resonator mode at $\omega_r$:

$$
H_{\mathrm{JC}} = \hbar\omega_r a^\dagger a - \frac{\hbar\omega_q}{2}\sigma_z + \hbar g\,(a^\dagger\sigma^- + a\,\sigma^+), \qquad \sigma^+ = |1\rangle\langle 0|, \tag{6.1}
$$

with the ground state $|0\rangle$ at $\sigma_z = +1$ (`T01 §2` and the conventions paragraph of `T04`; the circuit-QED literature usually writes $+\frac{\hbar\omega_q}{2}\sigma_z$ with the excited state at $\sigma_z = +1$, which flips the sign of every $\sigma_z$ term below and nothing else). $H_{\mathrm{JC}}$ conserves the excitation number and is block-diagonal in $\{|0,n+1\rangle, |1,n\rangle\}$ with eigenvalues $\hbar\omega_r(n+1) \pm \frac{\hbar}{2}\sqrt{\Delta^2 + 4g^2(n+1)}$, where $\Delta = \omega_q - \omega_r$. On resonance the vacuum Rabi splitting is $2g$.

### 6.2 Dispersive regime

For $|\Delta| \gg g\sqrt{n+1}$ a Schrieffer–Wolff transformation $U = \exp[\frac{g}{\Delta}(a^\dagger\sigma^- - a\sigma^+)]$ gives, to second order,

$$
H_{\mathrm{disp}} = \hbar\left(\omega_r - \chi\sigma_z\right)a^\dagger a - \frac{\hbar}{2}\left(\omega_q + \chi\right)\sigma_z, \qquad \chi_{\mathrm{2LS}} = \frac{g^2}{\Delta}, \tag{6.2}
$$

For a transmon the $|1\rangle\leftrightarrow|2\rangle$ transition (matrix element $\sqrt2 g$, detuning $\Delta + \alpha$) partially cancels the two-level shift:

$$
\chi = \frac{g^2}{\Delta}\,\frac{\alpha}{\Delta + \alpha}, \tag{6.3}
$$

so that $\chi \to 0$ as $\alpha \to 0$ (a harmonic oscillator produces no dispersive shift) and $\chi \to g^2/\Delta$ as $|\alpha| \to \infty$. The resonator frequency is $\omega_r - \chi$ with the qubit in $|0\rangle$ and $\omega_r + \chi$ in $|1\rangle$ — a state-dependent shift of $2\chi$ that is the readout signal. With the qubit below the resonator ($\Delta < 0$, $\chi < 0$) the ground state pushes the resonator *up* by $|\chi|$, as level repulsion requires; `T04 §10.4` quotes the same $2\chi$. The validity of (6.2) requires the photon number to stay below the critical number

$$
n_{\mathrm{crit}} = \frac{\Delta^2}{4g^2}. \tag{6.4}
$$

Numbers: $g/2\pi = 100$ MHz, $\omega_q/2\pi = 5.7$ GHz, $\omega_r/2\pi = 7.2$ GHz ($\Delta/2\pi = -1.5$ GHz), $\alpha/2\pi = -300$ MHz: $\chi/2\pi = -1.11$ MHz, $2\chi/2\pi = -2.2$ MHz, $n_{\mathrm{crit}} = 56$. The `Model` device files carry $g$, $\omega_r$, $\kappa$; the simulator computes $\chi$ from (6.3) using the exact transmon matrix elements, i.e. $\chi = g^2\left(\frac{|\langle 0|\hat n|1\rangle|^2}{\Delta_{01}} - \frac{|\langle 1|\hat n|2\rangle|^2}{\Delta_{12}}\right)/|\langle 0|\hat n|1\rangle|^2$ generalized over all levels.

### 6.3 Dispersive readout

Driving the resonator at its bare frequency $\omega_r$ with amplitude $\epsilon$ through a port of linewidth $\kappa$ gives steady-state coherent pointer states

$$
\alpha_{0,1} = \frac{\epsilon}{\kappa/2 \mp i\chi}, \qquad
\bar n = |\alpha_{0,1}|^2 = \frac{\epsilon^2}{\kappa^2/4 + \chi^2}, \qquad
|\alpha_0 - \alpha_1|^2 = \frac{4\chi^2\,\bar n}{\kappa^2/4 + \chi^2}. \tag{6.5}
$$

The pointer separation for fixed $\bar n$ is maximized at $2\chi = \kappa$, where $|\alpha_0 - \alpha_1|^2 = 2\bar n$. The measurement-induced dephasing rate of the qubit — the rate at which information leaves the resonator, equal to the measurement rate for an ideal detector — is

$$
\Gamma_m = \frac{\kappa}{2}\,|\alpha_0 - \alpha_1|^2 = \frac{2\kappa\chi^2\,\bar n}{\kappa^2/4 + \chi^2}, \qquad \Gamma_m\big|_{2\chi=\kappa} = \kappa\,\bar n, \qquad \Gamma_m\big|_{\chi\ll\kappa} = \frac{8\chi^2\bar n}{\kappa}. \tag{6.6}
$$

The $\chi \ll \kappa$ limit is the form quoted in `T04 §10.4`; (6.6) is the single derivation both documents and `T07 §6` rely on. With a measurement chain of quantum efficiency $\eta$ (`T07 §8`) and integration time $\tau \gg 1/\kappa$, the separation of the integrated quadrature signals in units of their standard deviation is

$$
\mathrm{SNR} = |\alpha_0 - \alpha_1|\sqrt{2\eta\kappa\tau}, \qquad
P_{\mathrm{err}} = \frac12\,\mathrm{erfc}\!\left(\frac{\mathrm{SNR}}{2\sqrt2}\right). \tag{6.7}
$$

Numbers (continuing §6.2 with $\kappa/2\pi = 2.2$ MHz, $\bar n = 5$, $\eta = 0.5$, $\tau = 500$ ns): $|\alpha_0-\alpha_1| = \sqrt{10} = 3.16$, $2\eta\kappa\tau = 6.9$, SNR $= 8.3$, $P_{\mathrm{err}} = 1.7\times10^{-5}$. The observed readout error of $0.5$–$2$ % on such devices is therefore dominated not by separation but by $T_1$ decay during $\tau$ (probability $\approx \tau/2T_1 = 0.25$ % for $T_1 = 100\ \mu$s), by state transitions induced at $\bar n \gtrsim n_{\mathrm{crit}}/5$, and by residual excited-state population. The digitizer model (`12 §4`) generates IQ clouds from (6.5)–(6.7) plus these three mechanisms.

### 6.4 Purcell decay and the Purcell filter

Through the same coupling, the qubit's excitation leaks out of the resonator port at the rate

$$
\gamma_P = \kappa\,\frac{g^2}{\Delta^2}, \tag{6.8}
$$

(two-level form; the transmon correction is a factor of order one). Numbers: $\kappa/2\pi = 2.2$ MHz, $g/\Delta = 1/15$ gives $\gamma_P = 2\pi\times 9.8$ kHz, a $T_1$ ceiling of $16\ \mu$s — unacceptable. A *Purcell filter* (a bandpass resonator or stub between the readout resonator and the feedline, passband centered on $\omega_r$ with bandwidth $\kappa_F$) suppresses the density of states at $\omega_q$ and multiplies (6.8) by $\approx (\kappa_F/2\Delta)^2\,(\omega_q/\omega_r)$, a factor of $10^{-2}$–$10^{-3}$ for $\kappa_F/2\pi \approx 100$ MHz, restoring $T_1 > 1$ ms limits. The lab scene models a common Purcell filter per feedline (`17 §3.5`), and the `Model` $T_1$ budget in `08 §4` includes the filtered Purcell term.

## 7. Single-qubit drive and DRAG

A drive voltage $V_d(t) = V_0(t)\cos(\omega_d t + \phi)$ on a line capacitively coupled to the transmon (coupling ratio $\beta = C_d/C_\Sigma$) adds $H_d = 2e\beta V_d(t)\,\hat n$. Projecting onto the Kerr levels, $\hat n \to i n_{\mathrm{zpf}}(b^\dagger - b)$, and moving to the frame rotating at $\omega_d$ with the rotating-wave approximation (`T07 §1`),

$$
H_d' = \frac{\hbar}{2}\Big[\Omega_x(t)\,(b + b^\dagger) + \Omega_y(t)\,i(b^\dagger - b)\Big] + \hbar\delta\, b^\dagger b + \frac{\hbar\alpha}{2}b^\dagger b^\dagger b b, \qquad
\Omega(t) = \frac{2e\beta V_0(t)\,n_{\mathrm{zpf}}}{\hbar}, \tag{7.1}
$$

with $\delta = \omega_{01} - \omega_d$ and $\Omega_x + i\Omega_y = \Omega e^{i\phi}$. In the two-level subspace, $b + b^\dagger \to X$ and $i(b^\dagger - b) \to Y$; a resonant pulse rotates the qubit about the axis at angle $\phi$ in the equatorial plane by

$$
\theta = \int_0^{t_g}\Omega(t)\,dt. \tag{7.2}
$$

The $|1\rangle\to|2\rangle$ transition is driven with matrix element $\sqrt2$ times larger and is detuned only by $\alpha$. A pulse of duration $t_g$ has spectral width $\sim 1/t_g$; for $t_g = 20$ ns and $|\alpha|/2\pi = 300$ MHz the leakage of a plain Gaussian $\pi$ pulse is of order $10^{-2}$.

**DRAG** (Derivative Removal by Adiabatic Gate). Applying the adiabatic-elimination transformation to (7.1) with three levels shows that leakage to $|2\rangle$ is cancelled to first order in $\Omega/\alpha$ by adding a quadrature component proportional to the derivative of the envelope, and that the resulting AC-Stark shift of $|1\rangle$ is cancelled by a detuning:

$$
\Omega_y(t) = -\frac{\dot\Omega_x(t)}{\alpha}, \qquad \delta(t) = -\frac{\Omega_x^2(t)}{2\alpha}\quad(\text{or the equivalent frame update}). \tag{7.3}
$$

The residual leakage after first-order DRAG scales as $(\Omega/\alpha)^4$; for $\Omega/2\pi = 25$ MHz ($t_g = 20$ ns Gaussian $\pi$ pulse, $\sigma = 5$ ns), $\alpha/2\pi = -300$ MHz, the simulated leakage falls from $\approx 1.5\times10^{-2}$ to $\approx 2\times10^{-5}$. Calibrations store a DRAG coefficient $\beta_D$ (units of time) in $\Omega_y = \beta_D\dot\Omega_x$ that is tuned empirically around $-1/\alpha$; the pulse-level backend integrates (7.1) with the stored $\beta_D$, and the difference from $-1/\alpha$ is one of the sources of the modelled single-qubit error.

The drive amplitude required at the chip follows from (7.1) and the attenuation chain; the numbers are worked in `T07 §3`.

## 8. The cross-resonance gate

Two fixed-frequency transmons — control $c$ and target $t$ — with exchange coupling $J$ (the $g$ of (5.1)) and $\Delta_{ct} = \omega_c - \omega_t$. Driving the *control* qubit at the *target's* frequency with amplitude $\Omega$ (defined as in (7.1)) produces, through the dressing of the control's states by $J$, an effective drive on the target whose sign depends on the control state.

**Derivation to first order in $J$.** Dress the states by the exchange term. $|0_c1_t\rangle$ acquires $-\frac{J}{\Delta_{ct}}|1_c0_t\rangle$; $|1_c0_t\rangle$ acquires $+\frac{J}{\Delta_{ct}}|0_c1_t\rangle$; $|1_c1_t\rangle$ acquires $-\frac{\sqrt2 J}{\Delta_{ct} + \alpha_c}|2_c0_t\rangle$. The drive operator $b_c + b_c^\dagger$ then has matrix elements between the dressed states with the control in $|0\rangle$ of $r_0 = -J/\Delta_{ct}$ and with the control in $|1\rangle$ of $r_1 = J/\Delta_{ct} - 2J/(\Delta_{ct} + \alpha_c)$ (the last term from the $|2_c\rangle$ admixture). Writing the effective Hamiltonian as

$$
H_{\mathrm{CR}} = \frac{\hbar}{2}\Big(\nu_{ZX}\,ZX + \nu_{IX}\,IX + \nu_{ZI}\,ZI + \nu_{ZZ}\,ZZ + \nu_{IY}\,IY + \ldots\Big), \tag{8.1}
$$

with $\nu_{ZX} = \Omega(r_0 - r_1)/2$ and $\nu_{IX} = \Omega(r_0 + r_1)/2$:

$$
\nu_{ZX} \simeq -\frac{J\,\Omega}{\Delta_{ct}}\,\frac{\alpha_c}{\Delta_{ct} + \alpha_c}, \qquad
\nu_{IX} \simeq -\frac{J\,\Omega}{\Delta_{ct} + \alpha_c}. \tag{8.2}
$$

In the two-level limit $\alpha_c \to -\infty$ this reduces to $\nu_{ZX} = -J\Omega/\Delta_{ct}$ and $\nu_{IX} = 0$. The $ZI$ term is the control's AC-Stark shift, $\nu_{ZI} \approx \Omega^2/2\Delta_{ct}$-order; $\nu_{ZZ}$ is the static $\zeta$ of (5.3). Higher orders in $\Omega$ saturate $\nu_{ZX}$ once $\Omega \gtrsim |\Delta_{ct} + \alpha_c|/2$ — the reason CR gates cannot be made arbitrarily fast and why $\Delta_{ct}$ is designed in the range $50$–$200$ MHz with the target *below* the control's $|1\rangle\to|2\rangle$ transition.

Numbers: $J/2\pi = 3$ MHz, $\Delta_{ct}/2\pi = +150$ MHz, $\alpha_c/2\pi = -330$ MHz, so $\Delta_{ct} + \alpha_c = -180$ MHz. Then $\nu_{ZX} = -0.0367\,\Omega$ and $\nu_{IX} = +0.0167\,\Omega$. For $\nu_{ZX}/2\pi = 2$ MHz the drive must be $\Omega/2\pi = 54$ MHz; the $ZX(\pi/2)$ rotation, $\exp(-i\frac{\pi}{4}ZX)$, takes $t = \frac{\pi/2}{\nu_{ZX}} = 125$ ns.

**Echoed CR and cancellation tone.** The unwanted $IX$, $ZI$, and $ZZ$ terms are removed by (i) an *echo*: two CR half-pulses of opposite sign separated by an $X_\pi$ on the control, which flips the sign of all $Z\otimes\cdot$ terms and cancels $IX$, $IY$, $ZI$ to first order while doubling $ZX$; and (ii) an *active cancellation tone* on the target at its own frequency with amplitude and phase chosen to null the residual $IX$ and $IY$ (measured by Hamiltonian tomography, `T10 §5`). Calibration files store the CR amplitude, phase, rise time, and cancellation amplitude/phase; the pulse-level backend reproduces the residual terms from (8.1) integrated numerically, which is how the `Model` two-qubit error emerges rather than being asserted.

**CNOT from $ZX(\pi/2)$.** With $ZX(\theta) \equiv \exp(-i\theta\, ZX/2)$,

$$
\mathrm{CNOT}_{c\to t} = \big(Z_{-\pi/2}\big)_c\,\big(X_{-\pi/2}\big)_t\;ZX(\pi/2)\;\;\text{(up to global phase)}, \tag{8.3}
$$

where the single-qubit corrections are applied *after* the $ZX$ pulse in circuit order; the $Z$ rotation on the control is a virtual-$Z$ frame update (`T07 §5`, zero duration) and the $X_{-\pi/2}$ on the target is a 20 ns pulse. Total echoed-CR CNOT duration on the example device: $2\times125$ ns (halves at $\nu_{ZX}$ doubled by echo compensate) $+\ 2\times20$ ns rise/fall $+\ 20$ ns control $\pi$ $+\ 20$ ns target correction $\approx 300$–$350$ ns, matching published fixed-frequency devices (`§11`).

## 9. Flux-activated CZ and iSWAP

For tunable transmons (or a tunable coupler) the two-qubit gates use the avoided crossing between $|11\rangle$ and $|02\rangle$ (or $|20\rangle$) at $\omega_1 = \omega_2 + \alpha_2$.

**Adiabatic CZ.** Move the pair to the vicinity of the crossing along a flux trajectory $\Phi(t)$ slow compared with $1/(\sqrt2 g)$, hold, and return. The computational states $|01\rangle, |10\rangle$ follow their own frequencies while $|11\rangle$ is repelled by $|02\rangle$; the accumulated conditional phase is

$$
\phi_{ZZ} = \int_0^{t_g}\Big[\omega_{11}(t) - \omega_{01}(t) - \omega_{10}(t)\Big]dt, \tag{9.1}
$$

and the gate is a CZ when $\phi_{ZZ} = \pi$ (modulo $2\pi$) with the single-qubit phases $\int(\omega_{01,10}(t) - \omega_{01,10}^{\mathrm{idle}})dt$ removed by virtual $Z$s. The trajectory is optimized (Slepian / "net-zero" shapes) so that the population that leaks to $|02\rangle$ during the excursion returns; residual leakage $10^{-3}$–$10^{-4}$ is the dominant CZ error. With tunable couplers the same physics is driven by the coupler flux while the qubits stay near their sweet spots, achieving $t_g = 30$–$60$ ns.

**Resonant iSWAP.** Bringing $\omega_1 = \omega_2$ for a time $t = \pi/2\tilde g$ produces $\mathrm{iSWAP} = \exp[-i\frac{\pi}{4}(XX + YY)]$; $t = \pi/4\tilde g$ gives $\sqrt{\mathrm{iSWAP}}$. During the gate the ZZ term (5.3) evaluated at $\Delta_{12} = 0$ adds a conditional phase $\approx 2\tilde g^2 t\,(\alpha_1 + \alpha_2)/(\alpha_1\alpha_2)$-order that is calibrated away; the pulse-level backend integrates it.

The `pulses.json` of tunable devices stores the flux waveform (shape, amplitude in $\Phi_0$, hold time); the Lindblad backend evaluates $\omega_{01}(\Phi(t))$ through (4.1) and (3.2) at each integration step (`10 §6`).

## 10. Decoherence mechanisms

| Mechanism | Rate form | Scaling / notes |
|-----------|-----------|-----------------|
| Dielectric loss (two-level systems, TLS) | $\Gamma_1^{\mathrm{TLS}} = \omega_{01}\sum_i p_i\tan\delta_i$ | $p_i$ = participation of interface $i$; $\tan\delta \sim 10^{-3}$ for oxides, $p \sim 10^{-3}$; weakly power- and temperature-dependent; the dominant $T_1$ limit in 2023–2025 planar transmons at $50$–$500\ \mu$s |
| Purcell | (6.8) with filter | $\propto\kappa g^2/\Delta^2$ |
| Quasiparticles | $\Gamma_1^{\mathrm{qp}} = x_{\mathrm{qp}}\sqrt{2\omega_{01}\Delta_{\mathrm{sc}}/\pi^2\hbar}$ | $x_{\mathrm{qp}} \sim 10^{-8}$–$10^{-6}$; also causes parity switching (charge dispersion jumps) at $\sim$ ms–s intervals |
| Thermal photons (readout line) | $\Gamma_\varphi^{\mathrm{th}} = \dfrac{4\chi^2\kappa\,n_{\mathrm{th}}}{\kappa^2 + 4\chi^2}$ | $n_{\mathrm{th}}$ from the attenuation chain (`T07 §9`); $\Gamma_\varphi^{\mathrm{th}} = \kappa n_{\mathrm{th}}/2$ at $2\chi=\kappa$ |
| Flux noise | (4.3) | $1/f$; zero at sweet spots |
| Charge noise | $\epsilon_1$ of (3.5) | $\lesssim 10$ kHz jumps; negligible for $E_J/E_C \ge 50$ |
| Residual ZZ | (5.3) | coherent, not decoherence, but appears as dephasing in ensemble-averaged experiments |

The total rates combine as $1/T_1 = \sum\Gamma_1$ and $1/T_2 = 1/2T_1 + \sum\Gamma_\varphi$; the relation between $T_2$, $T_2^*$, and the noise spectrum is in `T04 §8`. Calibration files carry measured $T_1$, $T_2^{\mathrm{echo}}$, $T_2^*$ per qubit; the mechanism table is used by the `Model` builder only when the user edits a physical parameter (e.g. adds attenuation) and asks the app to *re-derive* the calibration.

## 11. Typical parameters (planar transmon devices, 2023–2025)

| Parameter | Fixed-frequency, CR devices | Tunable-coupler devices | Notes |
|-----------|----------------------------|-------------------------|-------|
| $\omega_{01}/2\pi$ | 4.5–5.5 GHz | 4.0–7.0 GHz (parked) | heavy-hex frequency plan spans $\approx 300$ MHz |
| $\alpha/2\pi$ | $-300$ to $-350$ MHz | $-200$ to $-250$ MHz | |
| $E_J/E_C$ | 40–80 | 50–100 | |
| $T_1$ | 100–300 $\mu$s | 20–100 $\mu$s | |
| $T_2^{\mathrm{echo}}$ | 100–300 $\mu$s | 20–100 $\mu$s | |
| $T_2^*$ | 50–200 $\mu$s | 5–30 $\mu$s | flux noise on tunable qubits |
| $g/2\pi$ (qubit–qubit) | 2–5 MHz | 5–20 MHz ($\tilde g$ on) | |
| $\zeta/2\pi$ idle | 50–200 kHz | $<$ 10 kHz | |
| $g/2\pi$ (qubit–resonator) | 50–150 MHz | 50–150 MHz | |
| $\omega_r/2\pi$ | 6.5–7.5 GHz | 6.0–7.5 GHz | |
| $\kappa/2\pi$ | 0.5–5 MHz | 1–10 MHz | |
| $\chi/2\pi$ | 0.3–2 MHz | 0.5–3 MHz | |
| 1Q gate time | 20–40 ns | 15–30 ns | DRAG Gaussian, $\sigma = t_g/4$ |
| 2Q gate time | 250–500 ns (echoed CR) | 30–60 ns (CZ) | |
| 1Q error (RB) | $2$–$5\times10^{-4}$ | $5$–$10\times10^{-4}$ | |
| 2Q error (RB) | $5$–$15\times10^{-3}$ | $2$–$8\times10^{-3}$ | |
| Readout time | 300–1000 ns | 100–500 ns | + 100–300 ns ring-down |
| Readout error | 1–3 % | 0.5–2 % | |
| Reset | passive $5T_1$, or active $\sim 1\ \mu$s | active | |

These ranges parametrise `Assets/Devices/sc_*` calibrations; every value is tagged `Model` and its source range is displayed in the inspector.

## 12. Frequency allocation and collisions

For fixed-frequency devices with CR gates, a frequency plan must avoid the following *collisions* between neighbouring qubits $i, j$ (nearest neighbours on the coupling graph) and next-nearest neighbours $i, k$ sharing a neighbour $j$. With $\omega_{ij} \equiv \omega_i - \omega_j$ and a tolerance $\delta_{\mathrm{tol}} \approx 17$ MHz:

1. $|\omega_{ij}| < \delta_{\mathrm{tol}}$ — degenerate qubits, uncontrollable exchange.
2. $|\omega_{ij} - \alpha_j| < \delta_{\mathrm{tol}}$ or $|\omega_{ij} + \alpha_i| < \delta_{\mathrm{tol}}$ — $|01\rangle$ near $|12\rangle$/$|21\rangle$-type crossings, large ZZ.
3. $|2\omega_i - 2\omega_j - \alpha_j| < \delta_{\mathrm{tol}}$ — two-photon $|02\rangle\leftrightarrow|11\rangle$-type collision.
4. Control $i$ driven at target $j$'s frequency also drives a spectator $k$: $|\omega_{jk}| < \delta_{\mathrm{tol}}$ (spectator at the target frequency).
5. Spectator $|1\rangle\to|2\rangle$ at the CR drive frequency: $|\omega_j - \omega_k - \alpha_k| < \delta_{\mathrm{tol}}$.
6. Control driven at the target frequency hits its own $|1\rangle\to|2\rangle$: $|\omega_{ij} + \alpha_i| < \delta_{\mathrm{tol}}$ (already in 2) and the CR condition $\omega_j$ must lie *below* $\omega_i + \alpha_i$ for the usual sign of $\nu_{ZX}$.
7. For CR speed, $|\omega_{ij}|$ should lie in $[50, 200]$ MHz.

The *heavy-hex* lattice (degree $\le 3$) exists to make this feasible: with three neighbours a three-frequency plus offset pattern satisfies 1–7 for a fabrication frequency spread of $\sigma_f \approx 20$–$40$ MHz with acceptable yield; a square lattice of degree 4 with fixed frequencies does not, which is why square-lattice devices use tunable qubits or couplers. The device generator (`tools/devicegen`) checks conditions 1–7 for every `Assets/Devices/sc_*` file, and the inspector shows which condition a selected edge is closest to violating.

## Where this is used

| Result | Used by |
|--------|---------|
| (3.2) charge-basis Hamiltonian, exact eigenvalues | `09 §3` device physics, `12 §7` qubit spectroscopy instrument, `17 §3.5` chip inspector live values |
| (3.4)–(3.5) transmon limit, charge dispersion | theory overlays in the inspector; `08 §5` parity-switch noise |
| (3.6) Kerr model | `07 §6` Lindblad backend Hamiltonian |
| (4.1)–(4.3) flux tunability, flux-noise dephasing | `09 §3` tunable devices, `08 §4` derived $T_\varphi$, `12 §6` DC flux source |
| (5.1)–(5.3) couplings, ZZ | `09 §4` coupling map parameters, `08 §6` idle ZZ, `14 §8` echo insertion |
| (6.3)–(6.7) dispersive readout | `12 §4` digitizer IQ model, `12 §3` VNA resonator response, `15 §4` readout-error estimate |
| (6.8) Purcell | `08 §4` $T_1$ budget, `17 §3.5` Purcell filter component |
| (7.1)–(7.3) drive and DRAG | `10 §4` waveform library, `10 §5` gate calibrations |
| (8.1)–(8.3) cross-resonance | `10 §5` CR calibration, `14 §5` CNOT decomposition on CR devices, `15 §3` gate durations |
| (9.1) CZ / iSWAP | `10 §6` flux pulses, `14 §5` native gate set of tunable devices |
| §10 mechanisms | `08 §4` re-derivation of calibrations from physical edits |
| §11 ranges | `Assets/Devices/sc_*` (`09 §7`) |
| §12 collisions | `tools/devicegen`, `09 §5` topology validation |
