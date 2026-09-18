# T06 — Trapped-Ion Qubits

This document covers the physics of the `ion_chain_*` devices (`09 §6`): confinement in a
linear Paul trap, the collective motional modes that mediate two-qubit gates, the qubit
encodings, laser–ion interaction in the Lamb–Dicke regime, the Mølmer–Sørensen gate that the
pulse-level backend integrates, fluorescence state detection, and the error mechanisms that
parametrise the `Model` calibrations. All Rabi frequencies use the convention
$H = \frac{\hbar\Omega}{2}\sigma_x$ for a resonant carrier drive.

## 1. The linear Paul trap

### 1.1 RF confinement and the pseudopotential

A charged particle (charge $q = +e$, mass $m$) in the quadrupole potential
$\Phi(\mathbf r, t) = \frac{V_{\mathrm{RF}}\cos(\Omega_{\mathrm{RF}}t)}{2r_0^2}(x^2 - y^2) + \frac{\kappa U_{\mathrm{DC}}}{z_0^2}\left(z^2 - \tfrac{x^2+y^2}{2}\right)$
— $r_0$ the distance from the trap axis to the RF electrodes, $z_0$ the axial electrode half-spacing, $\kappa$ a geometric factor of order $0.1$–$0.5$ — obeys the Mathieu equation in each coordinate,

$$
\frac{d^2u}{d\xi^2} + \big(a_u - 2q_u\cos 2\xi\big)u = 0, \qquad \xi = \frac{\Omega_{\mathrm{RF}}t}{2}, \tag{1.1}
$$

with

$$
a_x = a_y = -\frac{a_z}{2} = -\frac{4e\kappa U_{\mathrm{DC}}}{m z_0^2\Omega_{\mathrm{RF}}^2}, \qquad
q_x = -q_y = \frac{2eV_{\mathrm{RF}}}{m r_0^2\Omega_{\mathrm{RF}}^2}, \quad q_z = 0. \tag{1.2}
$$

Stable trapping requires $(a, q)$ inside the lowest Mathieu stability region; for $a \approx 0$ the condition is $|q| < 0.908$, and traps operate at $q \approx 0.1$–$0.3$. For $|a|, q^2 \ll 1$ the motion separates into a fast *micromotion* at $\Omega_{\mathrm{RF}}$ and a slow *secular* motion in the time-averaged **pseudopotential**

$$
\Phi_{\mathrm{ps}}(r) = \frac{e^2 V_{\mathrm{RF}}^2}{4m\Omega_{\mathrm{RF}}^2 r_0^4}\,r^2 \quad(\text{energy}), \qquad r^2 = x^2 + y^2, \tag{1.3}
$$

giving secular frequencies

$$
\omega_{x,y} \simeq \frac{\Omega_{\mathrm{RF}}}{2}\sqrt{a_{x,y} + \frac{q_{x,y}^2}{2}}, \qquad
\omega_z = \frac{\Omega_{\mathrm{RF}}}{2}\sqrt{a_z} = \sqrt{\frac{2e\kappa U_{\mathrm{DC}}}{m z_0^2}}. \tag{1.4}
$$

Numbers ($^{171}$Yb$^+$, $m = 2.840\times10^{-25}$ kg; $\Omega_{\mathrm{RF}}/2\pi = 30$ MHz, $V_{\mathrm{RF}} = 200$ V, $r_0 = 250\ \mu$m): $q = 0.101$, $\omega_r/2\pi = 1.07$ MHz. Typical operating points: radial $\omega_{x,y}/2\pi = 1$–$5$ MHz, axial $\omega_z/2\pi = 0.1$–$1$ MHz, with $\omega_z < \omega_{x,y}$ so that the ions form a linear chain along $z$. Excess micromotion arises when stray DC fields push the ion off the RF null; it is compensated in the lab with shim voltages and is a `Model` parameter (residual modulation index) in the device file.

### 1.2 Equilibrium positions of a chain

$N$ ions on the axis with axial confinement $\frac12 m\omega_z^2 z^2$ and mutual Coulomb repulsion are in equilibrium where the force on each vanishes. With the length scale

$$
\ell^3 = \frac{e^2}{4\pi\varepsilon_0\, m\,\omega_z^2}, \qquad u_m \equiv z_m/\ell, \tag{1.5}
$$

the dimensionless equilibrium conditions are

$$
u_m - \sum_{n<m}\frac{1}{(u_m - u_n)^2} + \sum_{n>m}\frac{1}{(u_m - u_n)^2} = 0, \qquad m = 1,\ldots,N. \tag{1.6}
$$

Solutions: $N=2$: $u = \pm(1/4)^{1/3} = \pm0.630$; $N=3$: $0, \pm(5/4)^{1/3} = \pm1.077$. For general $N$ the simulator solves (1.6) by Newton iteration from an equally spaced start (converges in $<20$ iterations for $N \le 64$). The minimum spacing follows the fit $u_{\min}(N) \simeq 2.018\,N^{-0.559}$.

Numbers ($^{171}$Yb$^+$): $\omega_z/2\pi = 1$ MHz gives $\ell = 2.74\ \mu$m; two ions sit $3.45\ \mu$m apart. For an 11-ion chain at $\omega_z/2\pi = 0.3$ MHz, $\ell = 6.1\ \mu$m and $z_{\min} \approx 3.2\ \mu$m, with the chain spanning $\approx 45\ \mu$m. Individual addressing needs beam waists below $z_{\min}$; chains beyond $\sim 30$ ions at fixed $\omega_z$ buckle into a zigzag when $\omega_{r}/\omega_z < 0.73\,N^{0.86}$-order, which the device validator checks.

### 1.3 Normal modes

Expanding the potential to second order about equilibrium gives the axial Hessian (in units of $m\omega_z^2$)

$$
A_{nm} = \begin{cases}
1 + 2\displaystyle\sum_{p\ne n}\frac{1}{|u_n - u_p|^3}, & n = m,\\[10pt]
-\dfrac{2}{|u_n - u_m|^3}, & n \ne m.
\end{cases} \tag{1.7}
$$

Its eigenvalues $\mu_k$ give the axial mode frequencies $\omega_k = \sqrt{\mu_k}\,\omega_z$ and its eigenvectors $\mathbf b^{(k)}$ the participation of each ion. For every $N$: the centre-of-mass (COM) mode has $\mu_1 = 1$ with $b^{(1)}_n = 1/\sqrt N$, and the breathing mode has $\mu_2 = 3$ exactly. Radial modes use the Hessian $\delta_{nm}(\omega_r/\omega_z)^2 - \frac12(A_{nm} - \delta_{nm})$-type form (Coulomb *softens* radial confinement), so the radial COM mode is the *highest* radial mode and the radial spectrum is compressed into a band of width $\sim\omega_z^2 N^{0.9}/\omega_r$ below it. Two-qubit gates on chains address radial modes (better spectral isolation from axial heating, and all ions couple with comparable $|b_n^{(k)}|$).

The mode data ($\omega_k$, $\mathbf b^{(k)}$) are recomputed by `hw::IonChainModel` whenever $N$ or the trap frequencies change and are shown as a spectrum in the chip inspector (`17 §3.6`).

## 2. Qubit encodings

| Encoding | Example | Splitting | $T_1$ | Sensitivity | Readout |
|----------|---------|-----------|-------|-------------|---------|
| Hyperfine clock | $^{171}$Yb$^+$ $^2S_{1/2}\,|F=0,m_F=0\rangle \leftrightarrow |F=1,m_F=0\rangle$ | 12.642 812 118 GHz | effectively infinite (radiative lifetime $\gg$ years) | second-order Zeeman $310.8\,B^2$ Hz/G$^2$ | state-dependent fluorescence on $^2S_{1/2}\,F=1 \to {}^2P_{1/2}\,F=0$ at 369.5 nm |
| Hyperfine clock | $^{9}$Be$^+$, $^{43}$Ca$^+$, $^{137}$Ba$^+$ clock pairs at a field-insensitive point | 1.25 / 3.2 / 8.0 GHz | infinite | first-order insensitive at the magic field | fluorescence |
| Optical | $^{40}$Ca$^+$ $^2S_{1/2} \leftrightarrow {}^2D_{5/2}$ at 729 nm | 411 THz | $1.17$ s (D-state lifetime) | first-order Zeeman ($\sim$ MHz/G) | electron shelving: fluorescence on S–P at 397 nm; D state dark |
| Zeeman | $^{88}$Sr$^+$, $^{40}$Ca$^+$ ground-state $m_J = \pm1/2$ | $2.8\,B$ MHz/G | infinite | first-order (needs magnetic shielding/stabilization) | shelving to D then fluorescence |

The `ion_chain_*` device files (`09 §6`) use $^{171}$Yb$^+$ hyperfine clock qubits with Raman gates by default and $^{40}$Ca$^+$ optical qubits as the second option; the encoding tag selects the readout model (§7) and the dephasing model (§8).

## 3. Laser–ion interaction in the Lamb–Dicke regime

A travelling-wave field of wave vector $\mathbf k$ (or the effective $\Delta\mathbf k$ of a Raman pair) at angle $\theta$ to the mode axis couples the internal transition to the motion through the position operator $\hat z = z_{\mathrm{zpf}}(a + a^\dagger)$:

$$
H_{\mathrm{int}} = \frac{\hbar\Omega}{2}\Big(\sigma^+ e^{i(\eta(a + a^\dagger) - \delta t + \phi)} + \mathrm{h.c.}\Big), \qquad
\eta = k\cos\theta\sqrt{\frac{\hbar}{2m\omega}}, \tag{3.1}
$$

where $\delta = \omega_L - \omega_0$ is the laser detuning from the qubit transition and $\eta$ is the **Lamb–Dicke parameter** (for mode $k$ and ion $n$, $\eta_{n,k} = \eta\, b_n^{(k)}\sqrt{\omega/\omega_k}$-scaled). Expanding the exponential in the interaction picture of the motion, the resonances at $\delta = s\,\omega$ ($s \in \mathbb Z$) are the *carrier* ($s=0$), *blue sideband* ($s=+1$, $|{\downarrow},n\rangle\to|{\uparrow},n+1\rangle$), and *red sideband* ($s=-1$, $|{\downarrow},n\rangle\to|{\uparrow},n-1\rangle$), with Rabi frequencies

$$
\Omega_{n,n+s} = \Omega\, e^{-\eta^2/2}\,\eta^{|s|}\sqrt{\frac{n_<!}{n_>!}}\;L_{n_<}^{|s|}(\eta^2), \tag{3.2}
$$

$n_< = \min(n, n+s)$, $n_> = \max(n, n+s)$, $L$ the generalized Laguerre polynomial. In the **Lamb–Dicke regime**

$$
\eta^2(2\bar n + 1) \ll 1, \tag{3.3}
$$

(3.2) reduces to $\Omega_{n,n} \simeq \Omega(1 - \eta^2 n)$, $\Omega_{n,n+1} \simeq \eta\Omega\sqrt{n+1}$, $\Omega_{n,n-1} \simeq \eta\Omega\sqrt n$, and (3.1) becomes

$$
H_{\mathrm{LD}} = \frac{\hbar\Omega}{2}\Big[\sigma^+ e^{-i\delta t} + \mathrm{h.c.}\Big] + \frac{i\hbar\eta\Omega}{2}\Big[\sigma^+ e^{-i\delta t}(a e^{-i\omega t} + a^\dagger e^{i\omega t}) - \mathrm{h.c.}\Big]. \tag{3.4}
$$

Numbers ($^{171}$Yb$^+$, counter-propagating 355 nm Raman beams so $\Delta k = 2k = 3.54\times10^{7}$ m$^{-1}$, radial mode $\omega/2\pi = 3$ MHz): $z_{\mathrm{zpf}} = \sqrt{\hbar/2m\omega} = 3.14$ nm and $\eta = 0.11$. For $\bar n = 0.1$, $\eta^2(2\bar n + 1) = 0.015$.

**Raman transitions.** Hyperfine qubits are driven by two beams detuned by $\Delta_R$ from an excited state ($\Delta_R/2\pi \approx 33$ THz for the 355 nm Yb scheme, halfway between the $P_{1/2}$ and $P_{3/2}$ levels) whose frequency difference equals the qubit splitting. The effective Rabi frequency is $\Omega = \frac{\Omega_1\Omega_2}{2\Delta_R}$ (summed over intermediate states with signs), spontaneous scattering during a $\pi$ pulse scales as $\Gamma/\Delta_R$ and is $\sim 10^{-5}$–$10^{-4}$ per gate at these detunings; the effective wave vector is $\Delta\mathbf k = \mathbf k_1 - \mathbf k_2$, so co-propagating beams ($\Delta k \approx 0$) give motion-insensitive carrier gates and counter-propagating beams give the $\eta$ needed for sideband operations. Microwave drive at 12.6 GHz gives $\eta \approx 10^{-7}$: fine for single-qubit gates, useless for motional coupling unless a magnetic-field gradient is added.

## 4. Cooling

**Doppler cooling** on a dipole transition of linewidth $\Gamma$ reaches the limit

$$
k_B T_D = \frac{\hbar\Gamma}{2}, \qquad \bar n_D \simeq \frac{\Gamma}{2\omega}. \tag{4.1}
$$

Numbers (Yb 369.5 nm, $\Gamma/2\pi = 19.6$ MHz): $T_D = 0.47$ mK, $\bar n_D \approx 10$ at $\omega/2\pi = 1$ MHz and $\approx 3$ at 3 MHz.

**Resolved-sideband cooling** alternates red-sideband $\pi$ pulses ($|{\downarrow},n\rangle\to|{\uparrow},n-1\rangle$) with optical repumping ($|{\uparrow}\rangle\to|{\downarrow}\rangle$ without changing $n$ on average, because the repump is in the Lamb–Dicke regime). Each cycle removes one quantum; the steady state is set by off-resonant carrier and blue-sideband excitation,

$$
\bar n_{\mathrm{ss}} \simeq \left(\frac{\Gamma_{\mathrm{eff}}}{2\omega}\right)^2\left(\frac{\alpha_{\mathrm{rp}}}{4} + \frac14\right), \tag{4.2}
$$

where $\alpha_{\mathrm{rp}}$ is a geometric factor of the repump recoil; in practice $\bar n = 0.01$–$0.1$ on the gate modes after $\sim 1$–$5$ ms. Electromagnetically-induced-transparency (EIT) cooling reaches similar $\bar n$ on all modes simultaneously in $<1$ ms and is the default in the device timing model (`15 §3`).

## 5. Single-qubit gates

A resonant carrier pulse ($\delta = 0$) of duration $t$ and phase $\phi$ implements $R_\phi(\theta) = \exp[-i\frac{\theta}{2}(\cos\phi\,X + \sin\phi\,Y)]$ with $\theta = \Omega_{n,n}t$. Errors: the $n$-dependence of $\Omega_{n,n}$ in (3.2) (Debye–Waller factor) turns thermal motion into a pulse-area error $\sim\eta^2\bar n\,\theta$; with co-propagating Raman beams $\eta \to 0$ and this vanishes, leaving laser intensity noise and, for individual addressing, crosstalk from the beam tail on neighbouring ions (intensity ratio $10^{-2}$–$10^{-4}$, stored in the device file as a crosstalk matrix, `08 §7`). $Z$ rotations are phase updates of the subsequent pulses (virtual $Z$, zero duration). Typical durations: $1$–$10\ \mu$s (Raman), $10$–$50\ \mu$s (microwave); errors $10^{-5}$–$10^{-4}$.

## 6. The Mølmer–Sørensen gate

### 6.1 Bichromatic drive

Apply to ions $i$ and $j$ two tones at $\omega_0 \pm \mu$ with $\mu = \omega_k + \delta$: the red tone sits $\delta$ below the red sideband of mode $k$ and the blue tone $\delta$ above the blue sideband ($|\delta| \ll \omega_k$, and $|\delta|$ small compared with the spacing to other modes). Keeping the near-resonant terms of (3.4) for both ions and both tones, the carrier terms cancel in a rotating frame and the sideband terms combine into

$$
H_{\mathrm{MS}}(t) = \hbar g\,S_\phi\left(a\,e^{-i\delta t} + a^\dagger e^{i\delta t}\right), \qquad
g = \frac{\eta\Omega}{2}, \qquad
S_\phi = \sigma_\phi^{(i)} + \sigma_\phi^{(j)}, \tag{6.1}
$$

where $\sigma_\phi = \cos\phi\,X + \sin\phi\,Y$ with $\phi$ set by the tone phases, $\Omega$ is the carrier Rabi frequency of each tone, and equal Lamb–Dicke parameters are assumed (otherwise $g \to g_{i,j}$ per ion and $S_\phi$ is weighted).

### 6.2 Exact solution

Because $[H(t_1), H(t_2)]$ is a c-number times $S_\phi^2$, the Magnus expansion terminates at second order and the propagator is exact:

$$
U(t) = \exp\Big[S_\phi\big(\alpha(t)a^\dagger - \alpha^*(t)a\big)\Big]\exp\Big[i\Phi(t)\,S_\phi^2\Big], \qquad
\alpha(t) = -\frac{g}{\delta}\left(e^{i\delta t} - 1\right), \qquad
\Phi(t) = \frac{g^2}{\delta}\left(t - \frac{\sin\delta t}{\delta}\right). \tag{6.2}
$$

The first factor is a spin-dependent displacement of the mode: in phase space each spin eigenvalue of $S_\phi$ ($0, \pm2$) traces a circle of radius $2g/\delta$ per unit eigenvalue that closes whenever $\delta t = 2\pi K$. At

$$
\tau = \frac{2\pi K}{\delta}, \qquad K = 1, 2, \ldots \tag{6.3}
$$

the displacement vanishes for *every* motional state — the gate is insensitive to $\bar n$ to the extent that the Lamb–Dicke approximation holds — and the remaining factor, using $S_\phi^2 = 2I + 2\sigma_\phi^{(i)}\sigma_\phi^{(j)}$, is

$$
U(\tau) = \exp\Big[2i\Phi(\tau)\,\sigma_\phi^{(i)}\sigma_\phi^{(j)}\Big] \times(\text{global phase}), \qquad \Phi(\tau) = \frac{g^2\tau}{\delta}. \tag{6.4}
$$

Writing the gate in the standard form $XX(\theta) \equiv \exp(-i\frac{\theta}{2}XX)$ (for $\phi = 0$),

$$
\theta = -4\Phi(\tau) = -\frac{4g^2\tau}{\delta} = -\frac{\eta^2\Omega^2\tau}{\delta} = -\frac{2\pi K\,\eta^2\Omega^2}{\delta^2}. \tag{6.5}
$$

The maximally entangling gate $|\theta| = \pi/2$ requires

$$
|\delta| = 2\eta\Omega\sqrt K, \qquad \tau = \frac{\pi\sqrt K}{\eta\Omega}. \tag{6.6}
$$

$XX(\pi/2)$ is locally equivalent to CNOT: $\mathrm{CNOT}_{i\to j} = (Y_{-\pi/2})_i\,(X_{-\pi/2})_i(X_{-\pi/2})_j\;XX(\pi/2)\;(Y_{\pi/2})_i$ up to global phase (`T02 §6`; the compiler's decomposition table uses the sign-checked form). Smaller angles $XX(\theta)$, $|\theta| < \pi/2$, are native on ion devices and are obtained by scaling $\Omega$ at fixed $\tau$; the compiler exploits this for $R_{ZZ}(\theta)$-heavy circuits (`14 §5`).

Numbers: $\eta = 0.08$, $\Omega/2\pi = 500$ kHz, $K = 1$: $\delta/2\pi = 80$ kHz, $\tau = 12.5\ \mu$s. $K = 2$: $\delta/2\pi = 113$ kHz, $\tau = 17.7\ \mu$s. Published gates run $20$–$200\ \mu$s because $\Omega$ is limited by available laser power and by the requirement $|\delta| \ll$ mode spacing.

### 6.3 Error mechanisms

| Error | Form | Comment |
|-------|------|---------|
| Incomplete loop closure (timing or $\delta$ error $\epsilon_\delta$) | $\varepsilon \approx |\alpha_{\mathrm{res}}|^2(2\bar n + 1)$, $|\alpha_{\mathrm{res}}| \approx \frac{2g}{\delta}\pi K\frac{\epsilon_\delta}{\delta}$ | this is the *only* place $\bar n$ enters at leading order — hence Doppler-cooled gates are possible with shaped pulses |
| Spectator modes $k' \ne k$ | off-resonant displacement with $\delta_{k'} = \mu - \omega_{k'}$; residual $|\alpha_{k'}|^2 \sim (g_{k'}/\delta_{k'})^2$ | suppressed by amplitude modulation (AM), frequency modulation (FM), or multi-tone pulses that close all loops simultaneously; the device file specifies which (`10 §7`) |
| Motional heating during the gate | $\varepsilon \approx \dot{\bar n}\,\tau\,\frac{\eta^2\Omega^2}{\delta^2}$-order $= \dot{\bar n}\tau/(2K)$ for the $|\theta| = \pi/2$ condition | $\dot{\bar n} = 10$–$1000$ s$^{-1}$; for $\tau = 50\ \mu$s, $\dot{\bar n} = 100$ s$^{-1}$, $K=1$: $2.5\times10^{-3}$ |
| Motional dephasing (trap frequency drift $\Delta\omega_k$) | enters as $\epsilon_\delta$ above | requires $\Delta\omega_k/2\pi \ll 1$ kHz |
| Off-resonant carrier excitation | $\sim(\Omega/2\omega_k)^2$ per tone, oscillating at $\omega_k$ | suppressed by pulse edges longer than $1/\omega_k$ |
| Raman scattering | $\sim\tau\,\Gamma_{\mathrm{sc}}$ | $10^{-4}$-level at 33 THz detuning |
| Laser phase/intensity noise | $\delta\theta = \theta\,\delta\Omega/\Omega \times 2$ | $\Omega^2$ dependence doubles the sensitivity relative to carrier gates |
| Debye–Waller (beyond Lamb–Dicke) | $\sim\eta^2\bar n\,\theta$-type pulse-area error | keeps (3.3) as a validator condition |

Best published two-qubit errors: $(5$–$8)\times10^{-4}$ (Ca, Be, Yb, 2016–2024); commercial chains: $2$–$5\times10^{-3}$. The device file's `Model` two-qubit error is the sum of the table's terms evaluated for the stored parameters; the Lindblad backend integrates (6.1) with a heating Lindbladian ($\sqrt{\dot{\bar n}}\,a$, $\sqrt{\dot{\bar n}}\,a^\dagger$) and one spectator mode to reproduce it (`07 §6`).

## 7. State detection

### 7.1 Fluorescence

For a hyperfine qubit the bright state $|{\uparrow}\rangle = |F=1\rangle$ scatters photons on the cycling transition to $P_{1/2}$ at rate $R_{\mathrm{sc}} = \frac{\Gamma}{2}\frac{s}{1+s}$ (saturation parameter $s$, $\Gamma/2\pi = 19.6$ MHz for Yb), while $|{\downarrow}\rangle = |F=0\rangle$ is detuned by the hyperfine splitting and is dark. With collection efficiency $\epsilon_c$ (numerical aperture, filters, detector quantum efficiency; $\epsilon_c = 0.5$–$3$ % for lenses, $\gtrsim 10$ % with cavity or high-NA integrated optics), the mean counts in detection time $t_d$ are

$$
\lambda_b = \epsilon_c R_{\mathrm{sc}} t_d + \lambda_{\mathrm{bg}}, \qquad \lambda_d = \lambda_{\mathrm{bg}}, \tag{7.1}
$$

and the count distributions are Poissonian to first approximation, $P(n\,|\,\lambda) = e^{-\lambda}\lambda^n/n!$. A threshold $n_{\mathrm{th}}$ assigns $n \ge n_{\mathrm{th}} \to |{\uparrow}\rangle$ with errors

$$
\varepsilon_{\uparrow} = \sum_{n < n_{\mathrm{th}}}P(n\,|\,\lambda_b), \qquad
\varepsilon_{\downarrow} = \sum_{n \ge n_{\mathrm{th}}}P(n\,|\,\lambda_d). \tag{7.2}
$$

Numbers: $\lambda_b = 20$, $\lambda_d = 0.5$: $n_{\mathrm{th}} = 3$ gives $\varepsilon_\uparrow = 4.6\times10^{-7}$, $\varepsilon_\downarrow = 1.4\times10^{-2}$; $n_{\mathrm{th}} = 4$ gives $3.2\times10^{-6}$ and $1.8\times10^{-3}$; $n_{\mathrm{th}} = 5$ gives $1.7\times10^{-5}$ and $1.7\times10^{-4}$. The simulator picks $n_{\mathrm{th}}$ minimizing $(\varepsilon_\uparrow + \varepsilon_\downarrow)/2$.

The Poisson model omits the dominant real error: **off-resonant pumping** between the hyperfine manifolds during detection (bright $\to$ dark at rate $\sim R_{\mathrm{sc}}(\Gamma/2\omega_{\mathrm{hf}})^2$, dark $\to$ bright via the $F=1$ excited manifold), which truncates the bright distribution and adds a tail to the dark one. The instrument model (`12 §5`, photon-counter view) includes the pumping rates as `Model` parameters; the resulting detection errors saturate at $10^{-3}$–$10^{-4}$ for $t_d = 100$–$400\ \mu$s. Optical qubits detect by *electron shelving* (D state dark, S state bright on the 397/422 nm transition) with the same statistics and no pumping floor, reaching $10^{-4}$.

### 7.2 SPAM

State preparation is by optical pumping into $|{\downarrow}\rangle$ ($\sim 10\ \mu$s, error $10^{-4}$–$10^{-3}$). The combined *state preparation and measurement* (SPAM) error is what the calibration file's readout matrix (`08 §8`) encodes; mid-circuit measurement of one ion requires either shuttling it away or shelving spectators, adding $\sim 100\ \mu$s (`15 §3`).

## 8. Decoherence and drift

- **Hyperfine clock qubits:** $T_1 = \infty$ for practical purposes; $T_2^*$ is limited by magnetic-field noise through the second-order Zeeman shift ($310.8\,B^2$ Hz/G$^2$ for Yb; at $B = 5$ G, $\partial f/\partial B = 3.1$ kHz/G, so 1 mG noise gives 3 Hz) and by the Raman beams' differential AC-Stark shift during gates. Measured $T_2^*$: $0.1$–$1$ s; $T_2$ with dynamical decoupling: $>10$ s (up to $1$ hour demonstrated). The device model uses $T_2^* = 0.5$ s and treats dephasing as quasi-static (`T04 §5`).
- **Zeeman and optical qubits:** first-order sensitivity of $1.4$–$2.8$ MHz/G means $T_2^* = 1$–$10$ ms with $\mu$-metal shielding and current stabilization at $10^{-5}$; optical qubits additionally have $T_1 = 1.17$ s (Ca $D_{5/2}$) and laser linewidth limits.
- **Motional heating** $\dot{\bar n}$: electric-field noise from electrodes, $\dot{\bar n} \propto S_E(\omega)/\omega \propto d^{-4}$ ($d$ = ion–electrode distance), $S_E \propto T^{\beta}$ with $\beta \approx 1.5$–$2$ for room-temperature traps; cryogenic surface traps reduce $\dot{\bar n}$ by $10^2$. Typical: $10$–$100$ s$^{-1}$ (macroscopic trap, $d \sim 0.5$ mm), $10^2$–$10^4$ s$^{-1}$ (room-temperature surface trap, $d \sim 50$–$100\ \mu$m).
- **Trap frequency drift:** $10^{-4}$–$10^{-3}$ relative over minutes from RF amplitude drift; recalibration of $\delta$ on the minute scale is part of the modelled calibration cadence (`15 §5`).

## 9. Typical parameters (2023–2025 systems)

| Parameter | Value / range | Regime |
|-----------|---------------|--------|
| Ions per chain | 11–32 (commercial), up to 50–60 (research) | single zone |
| $\omega_z/2\pi$, $\omega_r/2\pi$ | 0.1–1 MHz, 1–5 MHz | |
| Ion spacing | 3–6 $\mu$m | |
| $\eta$ (radial, 355 nm Raman) | 0.05–0.12 | |
| $\bar n$ after sideband/EIT cooling | 0.01–0.1 | gate modes |
| $\dot{\bar n}$ | 10–1000 s$^{-1}$ | see §8 |
| 1Q gate time / error | 1–10 $\mu$s / $10^{-5}$–$10^{-4}$ | Raman |
| 2Q gate time / error | 20–200 $\mu$s / $5\times10^{-4}$–$5\times10^{-3}$ | MS, AM/FM-shaped |
| Connectivity | all-to-all within a chain | any pair via shared modes; fidelity degrades weakly with distance |
| Detection time / error | 100–400 $\mu$s / $10^{-4}$–$10^{-3}$ | fluorescence |
| State preparation | 10–20 $\mu$s / $10^{-4}$–$10^{-3}$ | optical pumping |
| Cooling per circuit | 1–5 ms | Doppler + sideband/EIT |
| $T_1$, $T_2^*$, $T_2^{\mathrm{DD}}$ | $\infty$, 0.1–1 s, $>10$ s | hyperfine clock |
| Circuit repetition rate | 10–100 Hz per shot cycle | dominated by cooling + detection |

These populate `Assets/Devices/ion_chain_11` and `ion_chain_32` (`09 §6`).

## 10. Scaling beyond one chain (QCCD)

The *quantum charge-coupled device* architecture divides a segmented trap into zones: loading, storage, gate, and detection. Ions are transported between zones by time-varying DC waveforms (transport at $\sim 1$ mm in $10$–$100\ \mu$s with heating of $\lesssim 1$ quantum), split and merged ($\sim 100\ \mu$s), and reordered by rotation in a junction. Sympathetic cooling with a second species ($^{138}$Ba$^+$ alongside Yb) re-cools the gate modes after transport without disturbing the qubit. The simulator's device model represents a QCCD as a graph of zones with transport-time edges; the compiler's router (`14 §7`) then optimizes transport rather than SWAPs, and the time estimator (`15 §3`) adds transport, split/merge, and re-cooling durations, which dominate: a 20-qubit circuit with 100 two-qubit gates on a two-zone QCCD spends $\sim 60$–$80$ % of its wall time in transport and cooling.

## 11. Worked examples used as test oracles

### 11.1 Mode spectrum of small chains

Solving (1.6)–(1.7) exactly (values to four figures; `25 §3` oracle for `hw::IonChainModel`):

| $N$ | Equilibrium $u_m$ | Axial $\mu_k$ | Axial $\omega_k/\omega_z$ |
|-----|-------------------|---------------|---------------------------|
| 2 | $\pm0.6300$ | $1,\ 3$ | $1,\ 1.7321$ |
| 3 | $0,\ \pm1.0772$ | $1,\ 3,\ 5.800$ | $1,\ 1.7321,\ 2.4083$ |
| 4 | $\pm0.5477,\ \pm1.4368$ | $1,\ 3,\ 5.810,\ 9.308$ | $1,\ 1.7321,\ 2.4104,\ 3.0509$ |
| 5 | $0,\ \pm0.9640,\ \pm1.8621$ | $1,\ 3,\ 5.818,\ 9.332,\ 13.47$ | $1,\ 1.7321,\ 2.4120,\ 3.0548,\ 3.6701$ |

The COM ($\mu=1$) and breathing ($\mu=3$) eigenvalues are exact for all $N$; the third mode ("Egyptian") tends to $\mu \to 5.82$ as $N$ grows.

### 11.2 Radial modes and gate-mode choice for $N = 5$

With $\omega_r/\omega_z = 10$ the radial eigenvalues are $\mu^{(r)}_k = (\omega_r/\omega_z)^2 - \tfrac12(\mu_k - 1)$, giving $\omega^{(r)}_k/\omega_z = 10,\ 9.950,\ 9.879,\ 9.790,\ 9.686$: a band of width $0.31\,\omega_z$ (94 kHz for $\omega_z/2\pi = 0.3$ MHz) below the radial COM mode. An MS gate with $\delta/2\pi = 20$ kHz on the radial COM mode has its nearest spectator at $\delta_{k'}/2\pi = 15 + 20 = 35$ kHz, requiring AM shaping (§6.3); addressing the *lowest* radial mode instead puts the nearest spectator at $31 + 20 = 51$ kHz. The device validator reports the ratio $\min_{k'}|\delta_{k'}|/|\delta|$ and warns below 3.

### 11.3 Amplitude-modulated MS pulse (closure of all loops)

With a piecewise-constant Rabi frequency $\Omega_s$ on segments $[t_{s-1}, t_s)$, the displacement of mode $k'$ at the end of the gate is

$$
\alpha_{k'}(\tau) = -\frac{\eta_{k'}}{2}\sum_s\Omega_s\,\frac{e^{i\delta_{k'}t_s} - e^{i\delta_{k'}t_{s-1}}}{\delta_{k'}}\,, \tag{11.1}
$$

and the accumulated $XX$ angle is $\theta = -\sum_{s,s'}\Omega_s\Omega_{s'}\,\eta_k^2\,\mathcal G_{ss'}$ with $\mathcal G$ the double integral of $\sin\delta_k(t-t')$ over the segment pair. Choosing $S$ segments and solving the $2M$ real closure conditions $\alpha_{k'}(\tau) = 0$ for the $M$ modes in the band, plus $\theta = -\pi/2$, is a linear-then-quadratic problem the `pulse` module solves by least squares (`10 §7`); $S \ge 2M + 1$ segments suffice. The pulse-level backend then integrates the resulting waveform against all $M$ modes and reports the residual $\sum_{k'}|\alpha_{k'}|^2(2\bar n_{k'} + 1)$ as the `Numerical` spectator error.

## Where this is used

| Result | Used by |
|--------|---------|
| (1.2)–(1.4) Mathieu parameters, secular frequencies | `09 §6` trap parameters, `17 §3.6` trap inspector, `12 §7` mode spectroscopy |
| (1.5)–(1.7) chain positions and normal modes | `hw::IonChainModel`, chip view geometry (`17 §3.6`), MS mode selection (`10 §7`) |
| (3.1)–(3.4) Lamb–Dicke Hamiltonian | `07 §6` Lindblad backend for ion devices |
| (4.1)–(4.2) cooling limits | `15 §3` cooling time and $\bar n$ in device files |
| §5 single-qubit gates | `10 §7` carrier pulses, `08 §7` addressing crosstalk |
| (6.1)–(6.6) MS gate | `10 §7` MS calibration (Ω, δ, K, shaping), `14 §5` CNOT/RZZ decomposition on ion devices, `15 §3` gate durations |
| §6.3 error table | `08 §4` `Model` two-qubit error for ion devices |
| (7.1)–(7.2) fluorescence detection | `12 §5` photon-counter instrument, `08 §8` readout matrix |
| §8 decoherence | `08 §4` ion noise model (quasi-static dephasing, heating) |
| §9 ranges | `Assets/Devices/ion_chain_*` (`09 §7`) |
| §10 QCCD | `09 §6` zone graph, `14 §7` transport-aware routing, `15 §3` |
