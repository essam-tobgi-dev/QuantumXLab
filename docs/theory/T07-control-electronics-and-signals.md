# T07 — Control Electronics and Signals

This document is the signal chain of the laboratory from the room-temperature instruments to
the qubit and back: how a generator setting becomes a Rabi frequency, how pulses are
synthesized and up-converted, how readout signals are demodulated and discriminated, how much
noise each amplifier adds, and how the attenuation chain sets the thermal photon number seen
by the qubit. Conventions: a resonant carrier drive is $H = \frac{\hbar\Omega}{2}\sigma_x$;
powers in dBm are $P_{\mathrm{dBm}} = 10\log_{10}(P/1\ \mathrm{mW})$; line impedance
$Z_0 = 50\ \Omega$; $A$ denotes a linear attenuation factor ($A = 10^{L_{\mathrm{dB}}/10} \ge 1$).

## 1. The rotating frame and the rotating-wave approximation

For a state $|\psi\rangle$ evolving under $H(t)$, define $|\psi'\rangle = U(t)|\psi\rangle$ with unitary $U(t)$. Then $i\hbar\,\partial_t|\psi'\rangle = H'|\psi'\rangle$ with

$$
H' = U H U^\dagger + i\hbar\,\dot U U^\dagger. \tag{1.1}
$$

For a qubit $H_0 = -\frac{\hbar\omega_q}{2}\sigma_z$ (ground state $|0\rangle$, the $+1$ eigenstate of $\sigma_z$, as in `T01 §2` and the conventions paragraph of `T04`) driven by $H_d = \hbar\Omega\cos(\omega_d t - \phi)\,\sigma_x$, choose $U = e^{-i\omega_d t\sigma_z/2}$, which cancels the free precession $e^{+i\omega_q t\sigma_z/2}$ of $H_0$ when $\omega_d = \omega_q$. Using $U\sigma_x U^\dagger = \sigma_x\cos\omega_d t + \sigma_y\sin\omega_d t$ and $i\hbar\dot UU^\dagger = +\frac{\hbar\omega_d}{2}\sigma_z$,

$$
H' = -\frac{\hbar\delta}{2}\sigma_z + \frac{\hbar\Omega}{2}\big(\cos\phi\,\sigma_x + \sin\phi\,\sigma_y\big) + \frac{\hbar\Omega}{2}\big[\cos(2\omega_dt - \phi)\sigma_x + \sin(2\omega_d t - \phi)\sigma_y\big], \qquad \delta = \omega_q - \omega_d. \tag{1.2}
$$

The **rotating-wave approximation** (RWA) drops the terms at $2\omega_d$. They average to zero over $\pi/\omega_d \approx 0.1$ ns and shift the resonance by the Bloch–Siegert amount $\delta_{\mathrm{BS}} = \Omega^2/4\omega_d$ — for $\Omega/2\pi = 50$ MHz at 5 GHz, $\delta_{\mathrm{BS}}/2\pi = 125$ kHz, which the calibration absorbs. The RWA is valid when $\Omega, |\delta| \ll \omega_d$; the Lindblad backend integrates in the rotating frame with the RWA by default and offers a lab-frame mode (`07 §6`) for validating it, at a cost of $\sim 10^3\times$ more time steps.

## 2. Rabi oscillations and Bloch equations

Under (1.2) with constant $\Omega$, $\phi = 0$, starting in $|0\rangle$, the excited-state population is

$$
P_1(t) = \frac{\Omega^2}{\Omega_R^2}\sin^2\!\left(\frac{\Omega_R t}{2}\right), \qquad \Omega_R = \sqrt{\Omega^2 + \delta^2}. \tag{2.1}
$$

On resonance a $\pi$ pulse ($\Omega t = \pi$) inverts the qubit; off resonance the contrast falls to $\Omega^2/\Omega_R^2$ and the oscillation speeds up. The *Rabi chevron* $P_1(\delta, t)$ is the first calibration experiment (`12 §7`) and its fit returns $\Omega$ and $\omega_q$.

With relaxation and dephasing the Bloch vector $\mathbf r = (\langle\sigma_x\rangle, \langle\sigma_y\rangle, \langle\sigma_z\rangle)$ obeys $\dot{\mathbf r} = \boldsymbol\omega\times\mathbf r$ plus damping, with $\boldsymbol\omega = (\Omega, 0, -\delta)$ read off from $H' = \frac{\hbar}{2}\boldsymbol\omega\cdot\boldsymbol\sigma$ at $\phi = 0$:

$$
\dot r_x = \delta\, r_y - \frac{r_x}{T_2}, \qquad
\dot r_y = -\delta\, r_x - \Omega\, r_z - \frac{r_y}{T_2}, \qquad
\dot r_z = \Omega\, r_y - \frac{r_z - r_z^{\mathrm{eq}}}{T_1}, \tag{2.2}
$$

with $r_z^{\mathrm{eq}} = +\tanh(\hbar\omega_q/2k_BT)$ ($\to +1$ at $T \to 0$): the ground state $|0\rangle$ sits at the north pole $r_z = +1$ and a resonant drive of area $\pi$ carries it to $r_z = -1$, the excited state $|1\rangle$ (`T01 §2`, `T04 §4.2`). Equations (2.2) are the two-level, Markovian limit of the Lindblad equation of `T04 §3` and are what the Bloch-sphere widget animates for a driven qubit (`21 §2`). In the convention $H_0 = +\frac{\hbar\omega_q}{2}\sigma_z$ used by much of the circuit-QED literature (excited state at $\sigma_z = +1$) the signs of $\delta$ and of $r_z^{\mathrm{eq}}$ flip; the physical predictions are identical.

## 3. From generator power to Rabi frequency

A signal generator (or AWG output after up-conversion) at power $P$ into $Z_0$ delivers a voltage amplitude

$$
V_0 = \sqrt{2Z_0 P}, \qquad P\,[\mathrm W] = 10^{(P_{\mathrm{dBm}} - 30)/10}. \tag{3.1}
$$

Numbers: $0$ dBm $= 1$ mW $\to V_0 = 316$ mV; $-20$ dBm $\to 31.6$ mV; $-80$ dBm $\to 31.6\ \mu$V.

The drive line (attenuation $A_{\mathrm{tot}}$ from all attenuators plus cable loss) delivers $V_{\mathrm{chip}} = V_0/\sqrt{A_{\mathrm{tot}}}$ to the chip; the on-chip drive line couples to the transmon through $C_d$ (`T05 §7`, coupling ratio $\beta = C_d/C_\Sigma$; with a $50\ \Omega$-terminated or open line the local voltage differs by a factor of order one absorbed into $\beta$). From (T05 7.1),

$$
\Omega = \frac{2e\,\beta\,V_{\mathrm{chip}}\,n_{\mathrm{zpf}}}{\hbar}, \qquad n_{\mathrm{zpf}} = \left(\frac{E_J}{32E_C}\right)^{1/4}. \tag{3.2}
$$

Worked chain for the T05 example transmon ($n_{\mathrm{zpf}} = 1.12$, $C_\Sigma = 64$ fF, $C_d = 0.1$ fF so $\beta = 1.56\times10^{-3}$): a $20$ ns Gaussian $\pi$ pulse ($\sigma = 5$ ns, area $\pi$) needs a peak $\Omega/2\pi = \pi/(\sqrt{2\pi}\sigma)/2\pi = 39.9$ MHz; take $\Omega/2\pi = 40$ MHz. Then $V_{\mathrm{chip}} = \hbar\Omega/(2e\beta n_{\mathrm{zpf}}) = 47\ \mu$V, $P_{\mathrm{chip}} = V_{\mathrm{chip}}^2/2Z_0 = 22$ pW $= -76.6$ dBm. With a 60 dB attenuation chain and 4 dB cable loss, the room-temperature instrument must deliver $-12.6$ dBm at the pulse peak — within an AWG's $\pm0.5$ V range after a mixer with $-6$ dB conversion loss. The `instr` model computes exactly this chain, so a user who removes a 20 dB attenuator sees the Rabi frequency rise by a factor 10 and the leakage grow accordingly (`12 §2`, `17 §3.2`).

**Area theorem.** For a resonant pulse with envelope $\Omega(t)$ the rotation angle is

$$
\theta = \int_{-\infty}^{\infty}\Omega(t)\,dt, \tag{3.3}
$$

independent of the shape (two-level, no decoherence). Shape matters for the off-resonant response, which is what leakage and spectator crosstalk are.

## 4. Pulse shapes and their spectra

| Shape | Envelope $\Omega(t)$ | Spectrum $|\tilde\Omega(\omega)|$ | Use |
|-------|----------------------|-----------------------------------|-----|
| Gaussian | $\Omega_0 e^{-t^2/2\sigma^2}$, truncated at $\pm t_c$ ($t_c = 2\sigma$ default), offset-subtracted to reach zero | $\propto e^{-\sigma^2\omega^2/2}$; $-40$ dB at $\omega = 3.03/\sigma$ | single-qubit gates; $t_g = 4\sigma$ |
| Gaussian-square (flat-top) | Gaussian rise/fall of width $\sigma_r$ around a flat segment of length $t_f$ | main lobe $\sim 1/t_f$ with Gaussian-suppressed sidelobes | cross-resonance, readout, MS |
| DRAG | $\Omega_x$ Gaussian, $\Omega_y = \beta_D\dot\Omega_x$ | quadrature spectrum $\propto\omega\tilde\Omega_x(\omega)$, a notch that is placed at $\alpha$ | single-qubit gates on transmons (`T05 §7`) |
| Cosine (Hann) | $\frac{\Omega_0}{2}(1 - \cos 2\pi t/t_g)$ | first sidelobe $-31$ dB | flux pulses |
| Slepian / net-zero | numerically optimal band-limited; zero net area | minimum spectral leakage for a given $t_g$ | adiabatic CZ (`T05 §9`) |
| Sinc-limited (raised cosine) | band-limited to $\pm B$ | rectangular spectrum | multiplexed readout tones |

Truncation of a Gaussian at $\pm2\sigma$ leaves a $13.5$ % step that spreads energy as $\mathrm{sinc}$ sidelobes; subtracting the offset $\Omega_0e^{-2}$ and renormalizing the area removes it. Offset subtraction is mandatory in the waveform library (`10 §4`). For the $20$ ns pulse above, the spectral amplitude at $|\alpha|/2\pi = 300$ MHz is $e^{-(\sigma\alpha)^2/2} = e^{-44}$ for the ideal Gaussian; the observed $\sim10^{-2}$ leakage without DRAG is not a spectral-tail effect but the strong-drive dynamics ($\Omega/|\alpha| = 0.13$), which is why DRAG rather than longer pulses is the fix.

## 5. IQ modulation, up-conversion, and frames

An AWG generates baseband $I(t), Q(t)$ (sample rate $f_s$, typically $1$–$2.5$ GS/s at 14–16 bit); an IQ mixer with a local oscillator (LO) at $\omega_{\mathrm{LO}}$ produces

$$
s(t) = I(t)\cos\omega_{\mathrm{LO}}t - Q(t)\sin\omega_{\mathrm{LO}}t = \mathrm{Re}\Big[\big(I(t) + iQ(t)\big)e^{i\omega_{\mathrm{LO}}t}\Big]. \tag{5.1}
$$

With $I + iQ = E(t)e^{i(\omega_{\mathrm{IF}}t + \phi)}$ the output is a single tone at $\omega_{\mathrm{LO}} + \omega_{\mathrm{IF}}$ with envelope $E(t)$ and phase $\phi$ (**single-sideband up-conversion**). One LO shared by several qubits with different $\omega_{\mathrm{IF}}$ (up to $\pm f_s/2$, in practice $\pm 400$ MHz) is the usual multiplexed drive.

Imperfections, each a `Model` parameter of the mixer component (`17 §3.3`):

- **LO leakage**: a residual tone at $\omega_{\mathrm{LO}}$ from DC offsets; suppressed by DC offset calibration to $-50$ to $-70$ dBc. Its effect on a qubit detuned by $\omega_{\mathrm{IF}}$ is an AC-Stark shift $\Omega_{\mathrm{leak}}^2/2\omega_{\mathrm{IF}}$ and off-resonant driving.
- **Image sideband** at $\omega_{\mathrm{LO}} - \omega_{\mathrm{IF}}$ from amplitude imbalance $\epsilon$ and phase skew $\varphi$ between the I and Q paths; image rejection ratio $\mathrm{IRR} \simeq \frac{\epsilon^2 + \varphi^2}{4}$ (for small $\epsilon, \varphi$), i.e. $1$ % amplitude imbalance and $1°$ skew give $-40$ dBc. Calibrated by predistorting $I, Q$ with the inverse $2\times2$ matrix.
- **Skew between I and Q sample clocks**: a frequency-dependent phase, corrected by an FIR predistortion.

**Frames and virtual $Z$.** The phase $\phi$ of every pulse on a qubit is referred to a software *frame* that tracks $\omega_q t + \phi_{\mathrm{frame}}$. A $Z$ rotation $R_z(\varphi)$ satisfies $R_z(\varphi)\,R_{\phi}(\theta) = R_{\phi + \varphi}(\theta)\,R_z(\varphi)$, so it can be commuted to the end of the circuit (and absorbed into the measurement basis, which is $Z$) by advancing the frame phase of all subsequent pulses by $\varphi$: **virtual $Z$**, exact and of zero duration. The OpenPulse `shift_phase` instruction is this operation (`13 §7`); the compiler's optimizer converts every $R_z$ on a transmon device into a frame update (`14 §6`).

## 6. Heterodyne readout and demodulation

The readout tone at $\omega_{\mathrm{ro}} = \omega_{\mathrm{LO}} + \omega_{\mathrm{IF}}$ passes through the resonator, acquires the state-dependent transmission (or reflection) of `T05 §6.3`, is amplified, and is down-converted with the *same* LO to $\omega_{\mathrm{IF}}$ (typically $10$–$100$ MHz), then digitized at $f_s = 1$ GS/s. The digitizer (or FPGA) computes the complex integrated signal

$$
S \equiv I + iQ = \frac{1}{T}\int_0^T s(t)\,w(t)\,e^{-i\omega_{\mathrm{IF}}t}\,dt \;\to\; \frac{1}{N}\sum_{n=0}^{N-1}s[n]\,w[n]\,e^{-i\omega_{\mathrm{IF}}n/f_s}, \tag{6.1}
$$

with weights $w(t)$. The expected trace for qubit state $j$ is $\langle s_j(t)\rangle \propto \mathrm{Re}[\alpha_j(t)e^{i\omega_{\mathrm{IF}}t}]$ with the ring-up $\alpha_j(t) = \alpha_j^{\mathrm{ss}}(1 - e^{-\kappa t/2}e^{\pm i\chi t})$ (drive at the bare resonator frequency). The **optimal weights** maximize the separation over noise:

$$
w(t) \propto \overline{\big(\langle s_1(t)\rangle - \langle s_0(t)\rangle\big)}\,e^{i\omega_{\mathrm{IF}}t}, \tag{6.2}
$$

the *matched filter*, obtained in the lab by averaging $\sim10^4$ traces prepared in $|0\rangle$ and $|1\rangle$ and subtracting. Boxcar weights ($w = 1$ on $[t_0, T]$) lose $\sim 20$–$40$ % of the SNR during ring-up; the digitizer model (`12 §4`) offers both.

After integration, shots form two Gaussian clouds in the IQ plane at $\mu_0, \mu_1$ with equal covariance $\sigma^2 I$ (for a linear chain). The optimal discriminator is the perpendicular bisector (rotate so that the separation lies along $I$, threshold at the midpoint); with $\mathrm{SNR} \equiv |\mu_1 - \mu_0|/\sigma$,

$$
P_{\mathrm{err}}^{\mathrm{sep}} = \frac12\,\mathrm{erfc}\!\left(\frac{\mathrm{SNR}}{2\sqrt2}\right), \tag{6.3}
$$

and the relation of SNR to the resonator parameters and integration time is (T05 6.7), $\mathrm{SNR} = |\alpha_0 - \alpha_1|\sqrt{2\eta\kappa\tau}$ with $\eta$ from §8. The full assignment error adds the $T_1$-during-readout term $\approx\tau_{\mathrm{eff}}/2T_1$ (for the $|1\rangle$ state, where $\tau_{\mathrm{eff}}$ is the weight-averaged integration time), residual thermal population, and measurement-induced transitions; the digitizer model produces the clouds with all four effects and the *readout calibration* instrument fits them (`12 §4`, `22 §5`) to produce the readout matrix of `08 §8`.

Multiplexed readout puts $N_{\mathrm{ro}} = 4$–$10$ tones on one feedline, spaced by $\ge 5\kappa$ in frequency; each is demodulated separately by (6.1). Cross-talk between tones (from amplifier compression and finite filter isolation) is a `Model` parameter in the feedline component.

## 7. Sampling, quantization, timing

- **Nyquist**: an AWG at $f_s$ synthesizes baseband up to $f_s/2$; sample-and-hold output has a $\mathrm{sinc}(f/f_s)$ roll-off ($-3.9$ dB at $f_s/2$) that the waveform generator pre-compensates.
- **Quantization**: $N$-bit uniform quantization of a full-scale sine has $\mathrm{SNR}_q = 6.02N + 1.76$ dB: $86$ dB for 14 bit, $98$ dB for 16 bit. The quantization noise floor of a $-12$ dBm full-scale pulse is far below the thermal noise after 60 dB of attenuation; but *amplitude resolution* matters: with 14 bits, the smallest step is $1.2\times10^{-4}$ of full scale, so a pulse at 10 % of full scale has $1.2\times10^{-3}$ relative amplitude resolution, an angle error of $1.2\times10^{-3}\times\pi = 3.8$ mrad per $\pi$ pulse, i.e. $\sim4\times10^{-6}$ infidelity — negligible, but it sets the rule that pulses should use $>30$ % of range. Digitizer quantization (8–14 bit at 1–2 GS/s) contributes noise $\ll$ HEMT noise when the gain is set so that the HEMT noise spans $\ge 8$ LSB.
- **Timing**: all instruments lock to a 10 MHz reference; channel-to-channel skew $\lesssim 10$ ps after calibration; sample jitter $\sim 1$ ps rms. A timing offset $\Delta t$ between an echoed-CR pulse and its cancellation tone produces a phase error $\omega_{\mathrm{IF}}\Delta t$ — for $\omega_{\mathrm{IF}}/2\pi = 100$ MHz and $\Delta t = 10$ ps, $6$ mrad. The scheduler (`14 §8`) works on a $\Delta t_{\min} = 1/f_s$ grid with all pulse starts aligned to it, and the AWG model rounds durations up to the grid (16-sample granularity is common; `10 §3`).
- **Phase noise** of the LO, $\mathcal L(f)$ in dBc/Hz, dephases the qubit relative to the frame: the accumulated phase variance over a time $t$ is $\sigma_\phi^2(t) = 2\int \mathcal L(f)\,\mathrm{sinc}^2(\pi f t)\cdot 4\sin^2(\pi f t)\,df$-type (a filter function); for a generator with $-110$ dBc/Hz at 1 kHz falling as $1/f^2$ to a floor of $-150$ dBc/Hz, the contribution to $T_\varphi$ exceeds 1 ms and is neglected in v1 except as a displayed `Model` value in the generator inspector.

## 8. Amplifier chain and noise temperature

An amplifier of gain $G$ that adds noise power $k_BT_NB$ (referred to its input, in bandwidth $B$) has *noise temperature* $T_N$. A cascade has (Friis)

$$
T_{\mathrm{sys}} = T_1 + \frac{T_2}{G_1} + \frac{T_3}{G_1G_2} + \cdots, \tag{8.1}
$$

so the first stage dominates provided $G_1 \gg T_2/T_1$. In photon-number units at frequency $f$, $n_N = k_BT_N/hf$ (valid for $k_BT_N \gg hf$; the exact relation is the Callen–Welton form $n_N + \tfrac12 = \tfrac12\coth(hf/2k_BT_N)$).

**Quantum limit (Caves).** A phase-preserving linear amplifier must add at least half a photon of noise referred to its input, $n_{\mathrm{add}} \ge \tfrac12$, so that together with the vacuum fluctuation of $\tfrac12$ photon in the signal the minimum total noise is one photon: $T_{\mathrm{sys}}^{\min} = hf/k_B = 0.24$ K at 5 GHz. A phase-*sensitive* amplifier (degenerate parametric amplifier) can amplify one quadrature noiselessly ($n_{\mathrm{add}} \to 0$) while deamplifying the other.

**Readout chain (superconducting devices).**

| Stage | Component | Gain | $T_N$ / $n_{\mathrm{add}}$ | Notes |
|-------|-----------|------|---------------------------|-------|
| MXC (10–20 mK) | Josephson parametric amplifier (JPA) or travelling-wave parametric amplifier (TWPA) | 15–25 dB | JPA phase-sensitive: $n_{\mathrm{add}} \approx 0.1$–$0.3$; TWPA phase-preserving: $n_{\mathrm{add}} \approx 1$–$2.5$ ($T_N \approx 0.3$–$0.6$ K) | JPA bandwidth 10–50 MHz, TWPA 2–4 GHz; pump at MXC ($\sim -70$ dBm) |
| MXC | 2–3 isolators / circulators | $-0.3$ to $-0.5$ dB each | — | protect the qubit from amplifier back-action; 20 dB isolation each |
| 4 K | HEMT (high-electron-mobility transistor) amplifier | 35–45 dB | $T_N = 1.5$–$4$ K | dissipates 5–20 mW at 4 K (`T08 §6`) |
| 300 K | low-noise amplifier(s) | 30–60 dB | $T_N = 50$–$300$ K | |
| 300 K | down-converter + digitizer | — | $\sim 10^3$ K equivalent | negligible after the chain |

Worked example at 5 GHz with a TWPA ($G_1 = 20$ dB $= 100$, $n_{\mathrm{add}} = 1.5$, i.e. $T_1 = 0.36$ K), HEMT ($G_2 = 40$ dB, $T_2 = 2.0$ K), room-temperature amplifier ($T_3 = 150$ K):
$T_{\mathrm{sys}} = 0.36 + 2.0/100 + 150/10^6 = 0.38$ K, i.e. $n_{\mathrm{sys}} = n_{\mathrm{add}} + \tfrac12 = 2.1$ photons. Without the TWPA: $T_{\mathrm{sys}} = 2.0 + 150/10^4 = 2.02$ K, $n_{\mathrm{sys}} = 8.9$.

**Measurement efficiency.** Define the quantum efficiency of the readout chain as

$$
\eta = \eta_{\mathrm{line}}\;\eta_{\mathrm{amp}}, \qquad \eta_{\mathrm{amp}} = \frac{1/2}{1/2 + n_{\mathrm{add}}^{\mathrm{tot}}}, \tag{8.2}
$$

where $\eta_{\mathrm{line}}$ ($0.5$–$0.8$) collects the transmission losses of circulators, isolators, connectors and cable between the resonator and the first amplifier (losses *before* amplification are lost signal; after it they are irrelevant), and $n_{\mathrm{add}}^{\mathrm{tot}}$ is the input-referred added noise of the whole cascade. For the TWPA chain above: $\eta_{\mathrm{amp}} = 0.5/2.02 = 0.25$, $\eta \approx 0.17$ with $\eta_{\mathrm{line}} = 0.7$. For a JPA in phase-sensitive mode with $n_{\mathrm{add}} = 0.15$ and the HEMT contribution $0.083$: $\eta_{\mathrm{amp}} = 0.5/0.73 = 0.68$, $\eta \approx 0.5$ — the value used in the `T05 §6.3` example. HEMT-only: $\eta \approx 0.04$, which is why parametric amplifiers are required for single-shot readout at $\tau < 1\ \mu$s. This $\eta$ enters (T05 6.7) and the digitizer model.

## 9. Thermal photons and the attenuation chain

A mode at frequency $f$ in equilibrium at temperature $T$ carries the Bose–Einstein mean photon number

$$
n_{\mathrm{th}}(f, T) = \frac{1}{e^{hf/k_BT} - 1}. \tag{9.1}
$$

At $f = 5$ GHz, $hf/k_B = 0.240$ K, and $n_{\mathrm{th}} = 1250$ (300 K), $208$ (50 K), $16.2$ (4 K), $2.86$ (0.8 K), $0.0996$ (100 mK), $6.1\times10^{-6}$ (20 mK), $1.6\times10^{-8}$ (15 mK).

An attenuator of factor $A$ at physical temperature $T_s$ transmits $1/A$ of the incoming photon flux and emits its own thermal radiation in proportion to its absorption:

$$
n_{\mathrm{out}} = \frac{n_{\mathrm{in}}}{A} + \left(1 - \frac1A\right)n_{\mathrm{th}}(f, T_s). \tag{9.2}
$$

Cables between stages are treated as distributed attenuators at a temperature interpolated along the length; in v1 the model lumps each cable's loss at its cold end (conservative by less than 10 %).

Worked examples use the standard chains of `11 §4` (stage names and nominal temperatures of `11 §1` / `T08 §3`: PT1 45 K, PT2 3.5 K, STILL 0.85 K, CP 0.10 K, MXC 15 mK), a 5 GHz mode, and an input at the 300 K thermal level ($n = 1250$; a generator's own noise floor is usually higher and the model uses the instrument's specified value). A thermal clamp without an attenuator ($A = 1$) passes the photon flux unchanged.

`drive_std` — 20 dB at PT2, 20 dB at MXC (40 dB):

| Stage | $T_s$ | $A$ (dB) | $n_{\mathrm{in}}$ | $n_{\mathrm{in}}/A$ | $(1-1/A)\,n_{\mathrm{th}}$ | $n_{\mathrm{out}}$ |
|-------|-------|----------|-------------------|---------------------|----------------------------|--------------------|
| PT1 | 45 K | 0 | 1250 | 1250 | 0 | 1250 |
| PT2 | 3.5 K | 20 | 1250 | 12.5 | 13.95 | 26.4 |
| STILL | 0.85 K | 0 | 26.4 | 26.4 | 0 | 26.4 |
| CP | 0.10 K | 0 | 26.4 | 26.4 | 0 | 26.4 |
| MXC | 15 mK | 20 | 26.4 | 0.264 | $1.1\times10^{-7}$ | **0.264** |

The line delivers $n = 0.26$ photons at 5 GHz, i.e. a radiative temperature $T_{\mathrm{eff}} = hf/[k_B\ln(1 + 1/n)] = 153$ mK: almost all of it is the 4 K plate's own emission (14 photons) attenuated by only 20 dB. `drive_60dB` (40 dB at PT2, 20 dB at MXC) barely helps — $n = 0.142$, $T_{\mathrm{eff}} = 115$ mK — because the extra attenuation sits *above* the 4 K emitter. Moving those 20 dB to the cold plate (20 dB at PT2, 20 dB at CP, 20 dB at MXC) gives $n = 3.6\times10^{-3}$ and $T_{\mathrm{eff}} = 43$ mK: the rule the inspector states is that each attenuator must cut the photons emitted by the stage *above* it, so the last 20–30 dB must sit at the MXC or CP. (This placement is what published 60 dB drive chains use; the `drive_60dB` variant of `11 §4` should be read with this table.)

`readout_in_std` — 20 dB at PT2, 10 dB at STILL, 20 dB at CP, 20 dB at MXC (70 dB):

| Stage | $T_s$ | $A$ (dB) | $n_{\mathrm{in}}$ | $n_{\mathrm{in}}/A$ | $(1-1/A)\,n_{\mathrm{th}}$ | $n_{\mathrm{out}}$ |
|-------|-------|----------|-------------------|---------------------|----------------------------|--------------------|
| PT1 | 45 K | 0 | 1250 | 1250 | 0 | 1250 |
| PT2 | 3.5 K | 20 | 1250 | 12.5 | 13.95 | 26.4 |
| STILL | 0.85 K | 10 | 26.4 | 2.64 | 2.76 | 5.40 |
| CP | 0.10 K | 20 | 5.40 | 0.054 | 0.099 | 0.153 |
| MXC | 15 mK | 20 | 0.153 | $1.5\times10^{-3}$ | $1.1\times10^{-7}$ | **$1.5\times10^{-3}$** |

$n = 1.5\times10^{-3}$, $T_{\mathrm{eff}} = 37$ mK — the wiring, not the mixing chamber, sets the radiative temperature seen by the resonator. Through the resonator these photons dephase the qubit at (`T05 §10`)

$$
\Gamma_\varphi^{\mathrm{th}} = \frac{4\chi^2\kappa\,n}{\kappa^2 + 4\chi^2} \xrightarrow{2\chi = \kappa} \frac{\kappa\,n}{2}: \qquad \kappa/2\pi = 2\ \mathrm{MHz},\ n = 1.5\times10^{-3} \Rightarrow T_\varphi = 104\ \mu\mathrm s, \tag{9.3}
$$

against $T_\varphi = 0.6\ \mu$s if the resonator saw the `drive_std` photon number. The 1–3 dB Eccosorb infrared filter at the MXC (`11 §4`) is not an attenuator in this budget (its loss is small) but blocks the $>100$ GHz radiation that the coaxial attenuators do not, which would otherwise break Cooper pairs (`T05 §10`).

The `cryo` module evaluates (9.2) for every line at the frequencies of every qubit and resonator and feeds the result into the noise model (`08 §4`) and the line inspector (`17 §3.2`), which shows these tables for the selected line.

The **output line** is the mirror problem: it needs *no* attenuation (signal) but must block room-temperature radiation from reaching the qubit — the job of the isolators (20 dB each, in series, at the MXC) and of the fact that the HEMT's input is at 4 K. The residual $n$ reaching the resonator from the output side is $n_{\mathrm{th}}(4\ \mathrm K)\times10^{-4}$ (two isolators) $+\ n_{\mathrm{th}}(15\ \mathrm{mK}) \approx 1.4\times10^{-3}$, comparable to the input line — a third isolator or a cold filter is common.

## 10. Qubit thermal population and effective temperature

In equilibrium at temperature $T$ the excited-state population of a qubit at $f_q$ is

$$
P_1 = \frac{1}{1 + e^{hf_q/k_BT}}, \qquad
T_{\mathrm{eff}} = \frac{hf_q}{k_B\ln\big[(1 - P_1)/P_1\big]}. \tag{10.1}
$$

For $f_q = 5$ GHz: $P_1 = 1$ % $\leftrightarrow T_{\mathrm{eff}} = 52$ mK; $0.1$ % $\leftrightarrow 35$ mK; at the mixing chamber's 15 mK, $P_1 = 1.1\times10^{-7}$. Measured residual populations of $0.1$–$2$ % show that the qubit is *not* in equilibrium with the mixing chamber: it thermalizes to the photon bath of its lines (§9) and to non-equilibrium quasiparticles. The device model sets $P_1$ from the *larger* of the line-derived $n$ (through $P_1 \approx n/(1+n)$-type coupling, weighted by each line's coupling rate to the qubit) and a stored `Model` floor; this population enters the noise model as the initial state and the thermal channel (`T04 §4`, `08 §5`) and is what the "effective qubit temperature" readout in the chip inspector reports. Active reset (measurement-conditioned $\pi$ pulse or resonator-assisted pumping) lowers the *pre-circuit* population below the thermal value at the cost of $\sim 1\ \mu$s; the estimator includes it (`15 §3`).

## 11. Flux bias lines, filters, and crosstalk

**Flux lines.** A current $I$ in a line with mutual inductance $M$ to the SQUID loop sets $\Phi = MI + \Phi_{\mathrm{off}}$; $M = 1$–$3$ pH, so one $\Phi_0$ needs $0.7$–$2$ mA. Static bias comes from a DC source through a $\sim 10$ kHz RC filter; fast flux pulses (CZ gates) come from an AWG channel through a $\sim 1$ GHz low-pass and a $20$ dB cold attenuator, with the line's dissipation at the MXC ($I^2R$ in the attenuator: $1$ mA through $50\ \Omega$ is $50\ \mu$W — far above the MXC budget; hence bias-tee designs terminate the DC path in a superconducting short and the fast path in a cold $50\ \Omega$ with $\lesssim 10\ \mu$A pulses). The line's transfer function (skin effect, reflections, bias-tee droop) distorts flux pulses on the $\mu$s scale; the calibration stores an FIR/IIR predistortion (`10 §6`) and the pulse-level backend applies the *un*-predistorted response so that a missing correction shows up as a CZ phase error.

**Filters.** A first-order RC low-pass has $f_c = 1/2\pi RC$; an $LC$ $\pi$-section $f_c = 1/2\pi\sqrt{LC}$ with $-40$ dB/decade. Above $\sim 20$ GHz commercial filters re-open, so an absorptive (Eccosorb) filter — a lossy dielectric whose attenuation rises with frequency, $\approx 1$ dB/cm at 5 GHz and $>30$ dB at 100 GHz for a 5 cm length — blocks the infrared and pair-breaking radiation that would generate quasiparticles. The cryo model treats it as a frequency-dependent attenuator at MXC temperature.

**Classical crosstalk.** A drive on line $i$ appears on qubit $j$ with relative amplitude $C_{ji}$ (from on-chip capacitive coupling and package modes; $-30$ to $-60$ dB for superconducting chips, $-20$ to $-40$ dB for optical addressing in ion chains). The effect on qubit $j$ is an off-resonant drive at $\omega_i$; for $|\omega_i - \omega_j| \gg C_{ji}\Omega$ it is an AC-Stark shift $\frac{(C_{ji}\Omega)^2}{2(\omega_j - \omega_i)}$, and for near-degenerate pairs it is a rotation error $C_{ji}\theta$. Flux crosstalk on tunable devices is a matrix $M_{ji}$ ($1$–$5$ % nearest neighbour) inverted by the calibration. Both matrices are `Model` parameters per device (`08 §7`); the compiler may apply pre-compensation by inverting them (`14 §9`).

## Where this is used

| Result | Used by |
|--------|---------|
| (1.1)–(1.2) rotating frame, RWA, Bloch–Siegert | `07 §6` Lindblad backend frame choice; `10 §2` frame semantics |
| (2.1)–(2.2) Rabi, Bloch equations | `12 §7` Rabi/chevron calibration fits (`22 §5`), `21 §2` Bloch-sphere animation |
| (3.1)–(3.3) power → voltage → Rabi rate, area theorem | `12 §2` generator/AWG models, `17 §3.2` line power budget, `10 §5` calibration amplitudes |
| §4 pulse table | `10 §4` waveform library and validator |
| (5.1) IQ modulation, mixer imperfections, virtual $Z$ | `12 §2` mixer model, `13 §7` `shift_phase`, `14 §6` $R_z$ elimination |
| (6.1)–(6.3) demodulation, weights, discrimination | `12 §4` digitizer model, `22 §5` readout calibration fit, `08 §8` readout matrix |
| §7 sampling/quantization/timing | `10 §3` time grid, `12 §2` AWG spec sheet, `14 §8` scheduler grid |
| (8.1)–(8.2) Friis, quantum limit, efficiency | `12 §4` chain noise, `17 §3.2` amplifier inspectors, `15 §4` readout-error estimate |
| (9.1)–(9.3) thermal photons through the chain | `cryo` line model (`11 §5`), `08 §4` photon dephasing, `17 §3.2` line inspector |
| (10.1) thermal population | `08 §5` initial state and thermal channel, `15 §3` reset time |
| §11 flux lines, filters, crosstalk | `10 §6` flux pulses/predistortion, `08 §7` crosstalk, `17 §3.2` filter components |
