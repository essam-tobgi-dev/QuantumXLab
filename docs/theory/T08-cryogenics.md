# T08 — Cryogenics

This document is the physics of the dilution refrigerator and its wiring as modelled by the
`cryo` module (`11`): how the mixture cools, how much cooling power each stage has, what heat
each coaxial line, attenuator, amplifier, and radiation path deposits, how long a cooldown
takes, and how the stage temperatures reach the qubit. All heat loads are in watts; all
temperatures in kelvin; thermal conductivity integrals in W/m. Every material value in this
document is a `Model`-class number: the curve fits behind them (NIST cryogenic material
properties database; Pobell, *Matter and Methods at Low Temperatures*; Ekin, *Experimental
Techniques for Low-Temperature Measurements*; Krinner et al., *EPJ Quantum Technology* 6:2,
2019) scatter by $\pm30$ % between alloy lots, and the app displays that uncertainty.

## 1. Dilution refrigeration

### 1.1 The $^3$He–$^4$He mixture

Below the tricritical point ($T_t = 0.867$ K, $^3$He concentration $x_t = 0.674$) a $^3$He–$^4$He liquid mixture separates into a $^3$He-rich *concentrated phase* (floating, lighter) and a $^4$He-rich *dilute phase*. The solubility of $^3$He in the dilute phase does not vanish as $T \to 0$: because a $^3$He atom is bound more strongly in $^4$He than in $^3$He (larger zero-point motion in pure $^3$He), the dilute phase retains

$$
x_3^{\mathrm{dil}}(T \to 0) = 6.6\ \%, \qquad x_3^{\mathrm{dil}}(T) \simeq 0.066\,(1 + 8.3\,T^2)\ \ (T \lesssim 0.1\ \mathrm K). \tag{1.1}
$$

The $^3$He in the dilute phase behaves as a degenerate Fermi gas of quasiparticles ($T_F \approx 0.4$ K at 6.6 %) moving through the superfluid $^4$He, which is thermodynamically inert below 0.5 K. The enthalpy per mole of $^3$He in each phase at low temperature is

$$
H_c(T) \simeq 12\,T^2\ \mathrm{J/mol}, \qquad H_d(T) \simeq 94\,T^2\ \mathrm{J/mol}, \tag{1.2}
$$

so moving $^3$He from the concentrated to the dilute phase across the phase boundary *absorbs* $H_d - H_c = 82\,T^2$ J/mol — the analogue of evaporation, but with a "vapour pressure" (the 6.6 % solubility) that does not vanish at $T \to 0$. This is the only continuous cooling mechanism that works below $\sim 0.3$ K.

### 1.2 Cooling power

With a $^3$He circulation rate $\dot n_3$ (mol/s) crossing the phase boundary in the **mixing chamber** (MXC) at temperature $T_{\mathrm{mc}}$, and the incoming concentrated $^3$He pre-cooled by the heat exchangers to $T_{\mathrm{ex}}$,

$$
\dot Q_{\mathrm{mc}} = \dot n_3\big[H_d(T_{\mathrm{mc}}) - H_c(T_{\mathrm{ex}})\big] \simeq \dot n_3\left(95\,T_{\mathrm{mc}}^2 - 11\,T_{\mathrm{ex}}^2\right)\ \mathrm W, \tag{1.3}
$$

(the coefficients 95 and 11 include the enthalpy of the $^3$He leaving the chamber in the dilute phase; Pobell §7). For ideal heat exchangers, $T_{\mathrm{ex}} = T_{\mathrm{mc}}$ and

$$
\dot Q_{\mathrm{mc}} = 84\,\dot n_3\,T_{\mathrm{mc}}^2. \tag{1.4}
$$

The simulator's thermal network (`11 §1`) uses (1.4) with an added parasitic load $\dot Q_0$, $\dot Q_{\mathrm{mc}} = 84\,\dot n_3 T_{\mathrm{mc}}^2 - \dot Q_0$, so that the base temperature is $\sqrt{\dot Q_0/84\dot n_3}$; the exchanger-limited form (1.3) is what the fridge inspector shows to explain *why* a base temperature exists. Setting $\dot Q_{\mathrm{mc}} = 0$ in (1.3) gives the base temperature $T_{\mathrm{mc}}^{\min} = \sqrt{11/95}\,T_{\mathrm{ex}} = 0.34\,T_{\mathrm{ex}}$: the final heat exchanger, not the mixture, limits the base temperature. Under a heat load $\dot Q$ the chamber warms to $T_{\mathrm{mc}} \simeq \sqrt{\dot Q/84\dot n_3}$ once $\dot Q \gg$ the intrinsic load.

Numbers: $\dot n_3 = 1$ mmol/s gives $840\ \mu$W at 100 mK, $34\ \mu$W at 20 mK, $8.4\ \mu$W at 10 mK. A commercial specification "$400\ \mu$W at 100 mK" corresponds to $\dot n_3 = 0.48$ mmol/s; the same fridge delivers $\approx 16\ \mu$W at 20 mK. The $T^2$ law is why every microwatt matters at base temperature and why the wiring budget of §6 is evaluated at the MXC.

### 1.3 The circulation loop

1. **Still** (0.7–0.9 K). The dilute phase is connected to the still, where $^3$He evaporates preferentially: at 0.7 K the vapour pressure of $^3$He is $\sim 10^3$ times that of $^4$He, so the vapour is $>97$ % $^3$He. A *still heater* supplies the latent heat, $\dot Q_{\mathrm{still}} = \dot n_3 L_3$ with $L_3 \approx 30$ J/mol at 0.7 K: $20$ mW of still power circulates $0.67$ mmol/s. The still power therefore *sets* $\dot n_3$ and hence the MXC cooling power; raising it also raises $T_{\mathrm{still}}$ and the $^4$He fraction in the vapour (film flow), which is suppressed by an orifice ("film burner"). Osmotic pressure between the still and the MXC drives the dilute-phase flow.
2. **Pumping and compression.** Room-temperature turbo and scroll (or dry compressor) pumps return the $^3$He at 0.5–3 bar through cold traps (charcoal at 77 K or 4 K removes air and water).
3. **Condensation.** In a *cryogen-free* system the returning gas is pre-cooled on the two pulse-tube stages (§2), then expanded through a flow impedance ($Z \approx 10^{11}$–$10^{12}$ m$^{-3}$) in a **Joule–Thomson** (JT) stage below the 4 K plate, where the isenthalpic expansion of the $\sim 2$ bar gas condenses it at $\sim 1$ K. Older wet systems used a pumped-$^4$He *1 K pot* for this step; the lab model uses the cryogen-free layout (`17 §3.1`).
4. **Heat exchangers.** The condensed $^3$He flows down through a *continuous counterflow* exchanger (concentric capillaries, incoming warm $^3$He inside, outgoing cold dilute stream outside) between the still and the cold plate, then through *step exchangers* (sintered silver, surface area $1$–$10$ m$^2$ each) between the cold plate and the MXC. Below 0.1 K the **Kapitza boundary resistance** between the liquid and the metal dominates,

$$
R_K = \frac{a}{A\,T^3}, \qquad a \approx 0.02\text{–}0.05\ \mathrm{m^2\,K^4/W}, \tag{1.5}
$$

so the required area grows as $T^{-3}$: passing $20\ \mu$W across $\Delta T = 5$ mK at 20 mK with $a = 0.02$ needs $A \approx 10$ m$^2$. Insufficient exchanger area raises $T_{\mathrm{ex}}$ and, through (1.3), the base temperature; the cryo model represents the exchanger by an effective $T_{\mathrm{ex}}(\dot n_3, T_{\mathrm{mc}})$ curve fitted to the manufacturer's specification.
5. **Cold plate** (CP, $\sim 100$ mK). A plate thermally anchored to the exchanger chain between the still and the MXC; it has $\sim 0.1$–$0.5$ mW of cooling and carries attenuators and filters that would overload the MXC.

## 2. Pulse-tube pre-cooling

A two-stage pulse-tube cryocooler (PT) provides the 50 K and 4 K stages without liquid cryogens. It compresses and expands helium gas at $\approx 1.4$ Hz; the *capacity map* of a common unit (PT415 class) is

| Stage | Temperature | Cooling power | Notes |
|-------|-------------|---------------|-------|
| 1st | 45 K | 40 W | up to $\sim 100$ W at 100 K during cooldown |
| 2nd | 4.2 K | 1.5 W | $0.9$ W at 3.5 K; no-load $\approx 2.8$ K |

The 4 K stage also pre-cools the incoming $^3$He and carries the HEMT amplifiers (§6). The compressor draws $\approx 8$–$12$ kW electrical; the pulse tube's cold head vibrates at the 1.4 Hz cycle with $\sim\mu$m amplitude, transmitted to the MXC unless decoupled by soft copper braids and an edge-welded bellows (`17 §3.1`); this vibration modulates flux through SQUID loops in a residual field gradient (§8) and is listed as a `Model` noise source in the pulse-tube inspector.

## 3. Stage temperatures

The model's nominal stage temperatures with the ranges observed across installed systems. The short names are the node ids of the thermal network in `11 §1`:

| Stage (short name) | Nominal $T$ | Range | Set by |
|--------------------|-------------|-------|--------|
| Room temperature (`RT`) | 293 K | 290–300 | lab |
| 50 K plate (`PT1`) | 45 K | 35–60 | PT 1st stage under load; supports the first radiation shield |
| 4 K plate (`PT2`) | 3.5 K | 2.8–4.5 | PT 2nd stage; HEMTs, isolators of the JT loop, coax thermalization |
| Still (`STILL`) | 0.85 K | 0.7–1.0 | still heater / $\dot n_3$ |
| Cold plate (`CP`) | 0.10 K | 0.06–0.15 | exchanger chain |
| Mixing chamber (`MXC`) | 15 mK | 7–25 | (1.3) with the total load |

Two vacuum-tight radiation shields (attached to the 50 K and 4 K plates) and a third, non-sealed shield at the still surround the lower stages; the outer vacuum can (OVC) at RT holds $\lesssim 10^{-6}$ mbar when cold (cryopumping). Each shield's interior is gold-plated or wrapped in multilayer insulation (MLI) on the outside.

## 4. Heat conduction along wiring

For a conductor of cross-section $A$ and length $L$ between temperatures $T_c < T_h$,

$$
\dot Q = \frac{A}{L}\int_{T_c}^{T_h}k(T)\,dT \equiv \frac{A}{L}\,\Theta(T_c, T_h), \tag{4.1}
$$

where $\Theta$ is the *thermal conductivity integral*. The cryo model stores $k(T)$ as NIST-style log-polynomial fits and integrates numerically; the table gives the resulting $\Theta$ between the model's nominal stage temperatures for the materials in the component catalog.

| Material | $\Theta$(50 K, 300 K) | $\Theta$(4 K, 50 K) | $\Theta$(0.85 K, 4 K) | $\Theta$(0.1 K, 0.85 K) | $\Theta$(0.015 K, 0.1 K) | Low-$T$ form of $k$ |
|----------|----------------------:|--------------------:|----------------------:|------------------------:|-------------------------:|---------------------|
| Stainless steel 304/316 | $2.8\times10^3$ | $1.4\times10^2$ | $0.52$ | $2.1\times10^{-2}$ | $3.3\times10^{-4}$ | $0.068\,T$ |
| CuNi (70/30) | $3.9\times10^3$ | $1.9\times10^2$ | $0.92$ | $3.8\times10^{-2}$ | $5.9\times10^{-4}$ | $0.12\,T$ |
| Beryllium copper (C17200) | $1.8\times10^4$ | $7\times10^2$ | $3.8$ | $0.16$ | $2.4\times10^{-3}$ | $0.5\,T$ |
| Brass (C26000) | $2.4\times10^4$ | $1.2\times10^3$ | $4.6$ | $0.19$ | $2.9\times10^{-3}$ | $0.6\,T$ |
| OFHC copper, RRR 100 | $1.1\times10^5$ | $4.6\times10^4$ | $1.1\times10^3$ | $45$ | $0.70$ | $143\,T$ (Wiedemann–Franz, $\rho = \rho_{300}/\mathrm{RRR}$) |
| NbTi ($T_c = 9.2$ K) | $1.6\times10^3$ | $1.0\times10^2$ | $0.20$ | $1.0\times10^{-3}$ | $2.5\times10^{-7}$ | $\sim 0.01\,T^3$ (phonon) below $T_c$ |
| PTFE (coax dielectric) | $60$ | $5.5$ | $0.10$ | $3.5\times10^{-3}$ | $1\times10^{-4}$ | $\sim 0.02\,T^{1.8}$ |
| Nylon / G-10 (supports) | $70$ | $6$ | $0.15$ | $5\times10^{-3}$ | $1.5\times10^{-4}$ | |

Reading the table: stainless and cupronickel are $10^{4}$–$10^{5}$ times poorer conductors than copper below 4 K, which is why every line crossing a stage boundary is stainless or CuNi, and why superconducting NbTi — whose electrons stop carrying heat below $T_c$ — is used for the readout output lines between 4 K and the MXC, where attenuation is not allowed. Copper appears only in thermal straps *within* a stage, where high conductance is the goal. Semi-rigid coax with a silver-plated copper-clad-steel centre conductor (SCC) conducts $\sim 10$–$50\times$ more than the all-stainless variant and is excluded from inter-stage runs in the catalog.

### 4.1 Coaxial line geometry

| Coax (outer diameter) | Outer conductor OD / ID | Centre conductor $\varnothing$ | $A_{\mathrm{outer}}$ | $A_{\mathrm{inner}}$ | $A_{\mathrm{diel}}$ | Loss at 5 GHz, 300 K (SS) |
|----------------------|------------------------|-------------------------------|---------------------:|---------------------:|--------------------:|---------------------------|
| 0.86 mm (UT-034 class) | 0.86 / 0.66 mm | 0.20 mm | 0.239 mm$^2$ | 0.031 mm$^2$ | 0.311 mm$^2$ | $\approx 8$ dB/m |
| 1.19 mm (UT-047 class) | 1.19 / 0.94 mm | 0.29 mm | 0.418 mm$^2$ | 0.066 mm$^2$ | 0.628 mm$^2$ | $\approx 5$ dB/m |
| 2.19 mm (UT-085 class) | 2.19 / 1.68 mm | 0.51 mm | 1.55 mm$^2$ | 0.204 mm$^2$ | 2.01 mm$^2$ | $\approx 2.5$ dB/m |

### 4.2 Heat load per line

Applying (4.1) to a 0.86 mm SS–SS line (both conductors stainless, PTFE dielectric) with the model's default run lengths between stages:

| Run | $L$ (m) | Metal (SS) | Dielectric (PTFE) | Total per line |
|-----|--------:|-----------:|------------------:|---------------:|
| RT → PT1 | 0.25 | 3.02 mW | 0.075 mW | **3.1 mW** |
| PT1 → PT2 | 0.20 | 0.19 mW | 0.009 mW | **0.20 mW** |
| PT2 → STILL | 0.15 | 0.94 $\mu$W | 0.21 $\mu$W | **1.1 $\mu$W** |
| STILL → CP | 0.10 | 57 nW | 11 nW | **68 nW** |
| CP → MXC | 0.10 | 0.9 nW | 0.3 nW | **1.2 nW** |

A 2.19 mm SS–SS line scales all entries by $\approx 6.5$ (RT → PT1: 20 mW). A 0.86 mm NbTi–NbTi line from PT2 downward: $0.36\ \mu$W (PT2 → STILL), $3$ nW (STILL → CP), $0.3$ nW (CP → MXC, dielectric-dominated). Cable *thermalization* at each stage (a clamp or a bulkhead attenuator) is assumed; a line that skips a stage anchor carries the full $\Theta(T_{\mathrm{lower}}, T_{\mathrm{upper}})$ across two stages and the lab's wiring validator (`11 §7`) flags it.

## 5. Radiation and other loads

**Radiation** between a warm surface $T_h$ and a cold surface $T_c$ of area $A$ with effective emissivity $\epsilon_{\mathrm{eff}}$ (for two parallel grey surfaces $\epsilon_{\mathrm{eff}} = (1/\epsilon_1 + 1/\epsilon_2 - 1)^{-1}$; with $n$ floating MLI layers divide by $n + 1$):

$$
\dot Q_{\mathrm{rad}} = \sigma A\,\epsilon_{\mathrm{eff}}\left(T_h^4 - T_c^4\right), \qquad \sigma = 5.670\times10^{-8}\ \mathrm{W\,m^{-2}K^{-4}}. \tag{5.1}
$$

Numbers for the model's geometry (OVC $\varnothing 0.5$ m $\times$ 1.2 m): RT → 50 K shield, $A = 2.0$ m$^2$: bare polished aluminium ($\epsilon_{\mathrm{eff}} \approx 0.03$) $\to 28$ W — most of the first stage's 40 W; with 10-layer MLI ($\epsilon_{\mathrm{eff}} \approx 0.004$) $\to 3.7$ W. 50 K → 4 K shield, $A = 1.5$ m$^2$, gold-plated copper $\epsilon_{\mathrm{eff}} = 0.02$: $10.6$ mW. 4 K → still: $0.3\ \mu$W. Below the still, radiation is negligible *except* for line-of-sight leaks through unclosed shield openings, which the lab model represents as an "open port" component: a 1 cm$^2$ hole in the 4 K shield views the 50 K shield and dumps $\sigma\cdot10^{-4}\cdot(50^4) = 35\ \mu$W onto the still — more than the still's wiring load.

**Residual gas conduction** at $10^{-6}$ mbar is $<1\ \mu$W across the 4 K shield and is neglected; a leak raising the OVC pressure to $10^{-4}$ mbar adds $\sim 100\ \mu$W to the 4 K stage and warms the MXC noticeably, which the model reproduces (it is the "vacuum leak" fault the user can inject, `11 §8`).

**Attenuator dissipation.** An attenuator of $L$ dB absorbs $(1 - 10^{-L/10})$ of the incident power. A drive line carrying a $-10$ dBm (100 $\mu$W) peak pulse into a 20 dB attenuator at 4 K deposits $99\ \mu$W at the peak; at a 10 % pulse duty cycle that is $10\ \mu$W average per line. At the MXC the incident power is already $10^{-3}$ of that, so a 20 dB MXC attenuator dissipates $\sim 100$ nW peak, $10$ nW average — small but, for $10^2$ lines, comparable to the entire wiring conduction load. Readout tones ($-30$ dBm at the chip, $\sim 1$ % duty) contribute negligibly. TWPA pumps ($-70$ dBm, continuous, absorbed in the MXC isolator) add $\sim 0.1$ nW each.

**Amplifiers.** A HEMT dissipates its DC bias power, 5–20 mW, at the 4 K stage. This is the single largest 4 K load in a multi-line system: 8 HEMTs $\approx 100$ mW versus 1.5 W available.

**Mechanical supports.** G-10 or stainless struts between plates carry loads comparable to $\sim 20$ stainless coax lines and are included as fixed per-stage `Model` values (RT → PT1: 60 mW; PT1 → PT2: 5 mW; below: negligible).

## 6. Worked wiring budget: 27-qubit fixed-frequency transmon device

Line count (heavy-hex, fixed-frequency, cross-resonance; `Assets/Devices/sc_heavyhex_27`): 27 drive lines (0.86 mm SS–SS), 4 readout input lines (0.86 mm SS–SS), 4 readout output lines (NbTi–NbTi below PT2, SS–SS above), 4 TWPA pump lines (SS–SS), no flux lines. Total 39 lines from RT to PT2; 35 SS–SS and 4 NbTi below PT2. Attenuation plan: drive 20/10/30 dB (PT2/STILL/MXC); readout input 20/10/40 dB; pump 20/10/20 dB. Four HEMTs at PT2, two isolators per output line at the MXC.

| Stage | Conduction (lines) | Attenuator / amplifier dissipation | Radiation + supports | Total load | Available | Margin |
|-------|-------------------:|-----------------------------------:|---------------------:|-----------:|----------:|--------|
| PT1 | $39\times3.1$ mW $= 0.12$ W | — | 3.7 W + 0.06 W | **3.9 W** | 40 W | 10× |
| PT2 | $39\times0.20$ mW $= 7.8$ mW | 35 lines $\times\ 10\ \mu$W (20 dB, 10 % duty) $= 0.35$ mW; 4 HEMT $\times 15$ mW $= 60$ mW | 10.6 mW + 5 mW | **84 mW** | 1.5 W | 18× |
| STILL | $35\times1.1\ \mu$W $+ 4\times0.36\ \mu$W $= 40\ \mu$W | 35 $\times$ 1 $\mu$W (10 dB) $= 35\ \mu$W | 0.3 $\mu$W | **75 $\mu$W** | 10–20 mW | $>100\times$ |
| CP | $35\times68$ nW $+ 4\times3$ nW $= 2.4\ \mu$W | — | — | **2.4 $\mu$W** | 100–300 $\mu$W | $>40\times$ |
| MXC | $35\times1.2$ nW $+ 4\times0.3$ nW $= 43$ nW | 27 drive $\times$ 10 nW $+$ 4 readout $\times$ 0.1 nW $+$ 4 pumps $\times$ 0.1 nW $= 0.27\ \mu$W | — | **0.31 $\mu$W** | 16 $\mu$W at 20 mK | 50× |

Conclusions the model surfaces in the fridge inspector: (i) at 27 qubits no stage is near its limit; the MXC load of $0.3\ \mu$W raises $T_{\mathrm{mc}}$ by $\sqrt{(Q_0 + 0.3\ \mu\mathrm W)/84\dot n_3} - \sqrt{Q_0/84\dot n_3} \approx 0.2$ mK above the intrinsic base. (ii) Scaling the same wiring to 127 qubits (≈ 150 lines, 16 HEMTs) puts the PT2 stage at $\approx 0.35$ W (still within 1.5 W but with the JT pre-cooling load also on that stage) and the MXC at $\approx 1.5\ \mu$W — a 10 % rise in cooling-power consumption; the binding constraints become HEMT count and *physical* feedthrough area (39 × 0.86 mm lines occupy one $\varnothing 50$ mm port; 150 lines need four), which the lab's port-capacity validator reports (`11 §7`). (iii) Adding flux lines for a tunable device doubles the drive-line count and adds DC dissipation in the bias network; the same table is regenerated for `sc_tunable_coupler_54`.

## 7. Cooldown dynamics

Each stage is a lumped thermal mass $C_i(T_i)$ (J/K) coupled to its cooler and to neighbouring stages:

$$
C_i(T_i)\,\dot T_i = \dot Q_{\mathrm{cool},i}(T_i) - \sum_j\dot Q_{ij}(T_i, T_j) - \dot Q_{\mathrm{load},i}, \tag{7.1}
$$

where $\dot Q_{\mathrm{cool},i}$ is the PT capacity map (§2) or the dilution cooling power (1.3), $\dot Q_{ij}$ the conduction (4.1) and radiation (5.1) exchanges, and $\dot Q_{\mathrm{load}}$ the dissipations of §5. The cryo module integrates (7.1) with an implicit Euler step (stiff below 1 K; `06 §6`) at $\Delta t = 10$ s for the cooldown and $\Delta t = 1$ s for operating-point dynamics.

**Heat capacity.** The Debye lattice term plus the electronic term,

$$
C = 9Nk_B\left(\frac{T}{\Theta_D}\right)^3\int_0^{\Theta_D/T}\frac{x^4e^x}{(e^x - 1)^2}\,dx + \gamma T
\;\xrightarrow{T \ll \Theta_D}\; \frac{12\pi^4}{5}Nk_B\left(\frac{T}{\Theta_D}\right)^3 + \gamma T, \tag{7.2}
$$

with $\Theta_D = 343$ K, $\gamma = 0.695$ mJ mol$^{-1}$K$^{-2}$ for copper ($C = 385$ J kg$^{-1}$K$^{-1}$ at 300 K, $0.092$ J kg$^{-1}$K$^{-1}$ at 4 K, $2.3\times10^{-4}$ J kg$^{-1}$K$^{-1}$ at 20 mK) and $\Theta_D \approx 470$ K, $\gamma \approx 0.46$ mJ mol$^{-1}$K$^{-2}$ for stainless steel. The enthalpy of copper from 4 K to 300 K is $79.6$ kJ/kg; a fridge with 50 kg of copper and stainless below the 50 K stage stores $\approx 4$ MJ, and with the PT's first stage delivering $40$–$100$ W net during the descent the 300 → 4 K phase takes $\approx 4\times10^6/50 \approx 22$ h.

**Timeline reproduced by the model** (nominal system, 27-qubit payload): 0–2 h pump-out of the OVC; 2–28 h pulse-tube cooldown of all stages to $\approx 4$ K (the still, CP, and MXC follow through the exchanger chain and, in cryogen-free systems, through a heat switch or exchange gas); 28–30 h mixture condensation (the $^3$He/$^4$He charge, $\sim 20$ L STP, is compressed in, cooled on the PT stages, and condenses through the JT impedance); 30–36 h dilution cooling from $\sim 1$ K to base, the last decade slowed by the $T^2$ cooling power and $T^{-3}$ Kapitza resistance; after 36–40 h the MXC reaches its base and the qubit temperatures (§9) settle over a further 12–24 h as the package and cables equilibrate. Warm-up with the heaters and the PT off takes $\approx 24$ h; a *warm-up to 4 K for a sample change* is not possible without a load-lock, which the lab model offers as an optional component (`17 §3.1`, cool time of the puck alone $\approx 8$ h).

## 8. Thermometry, vibration, and magnetic shielding

**Thermometers.** Each stage carries a resistance thermometer read by a four-wire AC bridge (`12 §6`):

| Sensor | Range | Physics | Resolution / caveats |
|--------|-------|---------|----------------------|
| Platinum (PT100) | 30–300 K | metal resistivity | $\pm 0.1$ K |
| Cernox (zirconium oxynitride) | 0.1–300 K | negative-temperature-coefficient film | $\pm 1$ % of $T$; magnetic-field insensitive |
| Ruthenium oxide (RuO$_2$) | 10 mK–40 K | variable-range hopping, $R \propto \exp[(T_0/T)^{1/4}]$ | $\pm 0.5$ mK at 20 mK with $\lesssim 1$ pW excitation; self-heating $\Delta T = P\,R_{\mathrm{th}}$ with $R_{\mathrm{th}} \propto T^{-3}$ (Kapitza): 1 pW at 10 mK raises the reading $\sim 1$ mK |
| CMN (cerium magnesium nitrate) | 2 mK–2 K | Curie susceptibility $\chi = C/(T - \Delta)$ | primary-like; slow ($\sim$ min) |
| Noise thermometer (MFFT) | 1 mK–1 K | Johnson noise $\langle V^2\rangle = 4k_BTR\,\Delta f$ via a SQUID | primary; $\pm 1$ % in $\sim 100$ s |
| Coulomb-blockade thermometer | 10 mK–1 K | conductance-dip width $\propto T$ | primary; $\pm 1$ % |

The lab's MXC thermometer is an RuO$_2$ with a stored self-heating model, so that raising the bridge excitation in the instrument panel visibly raises the reading without changing the true temperature — a training behaviour, labelled `Model`.

**Vibration.** The pulse tube's 1.4 Hz cycle and the turbo pump's rotation (∼1 kHz) reach the MXC at $\sim 0.1$–$1\ \mu$m amplitude; in a residual field gradient of $\nabla B \sim 1\ \mu$T/mm, a $1\ \mu$m displacement modulates the flux through a $100\ \mu$m$^2$ SQUID loop by $\Delta\Phi = A\,\nabla B\,\Delta x = 10^{-10}\cdot10^{-3}\cdot10^{-6} = 10^{-19}$ Wb $= 5\times10^{-5}\,\Phi_0$ — at the level of the $1/f$ flux noise integrated over seconds, and visible as a 1.4 Hz line in the Ramsey-frequency time series of tunable qubits. Cables also generate triboelectric charge noise when flexed.

**Magnetic shielding.** Three requirements: (i) a superconducting film must be cooled through $T_c$ in $B \lesssim 1\ \mu$T to avoid trapping vortices, which act as TLS-like loss centres; (ii) a SQUID loop of $100\ \mu$m$^2$ in $1\ \mu$T carries $\Phi = 10^{-16}$ Wb $= 0.05\,\Phi_0$, so the DC field must be stable to $\ll 1\ \mu$T for tunable qubits to remain at their sweet spots; (iii) low-frequency field noise appears as flux noise. The catalog's shielding is a $\mu$-metal (Cryoperm) can at the 4 K stage (attenuation $\sim 10^2$–$10^3$ for the Earth's $50\ \mu$T) around an aluminium or tin superconducting can at the MXC (Meissner expulsion, attenuation $>10^3$ once cold, provided the field was already $<\ \mu$T when it went superconducting — hence the $\mu$-metal outside). The resulting residual field target is $<0.1\ \mu$T at the chip; the inspector reports the `Model` residual for the selected shield configuration and warns if the user removes the outer can.

## 9. From stage temperature to the qubit

Three baths couple to the qubit; the model evaluates each and reports the dominant one:

1. **Photon bath of the lines** — the effective temperature $T_{\mathrm{line}}$ of each drive and readout line at the qubit's frequency from the attenuation chain (T07 §9, eq. 9.2), typically 40–60 mK for a 60 dB chain. This sets the residual excited-state population and the photon-induced dephasing (T07 §9–10).
2. **Phonon bath of the substrate** — the chip is thermally anchored to the MXC through its package; with a copper package and $\gtrsim 10$ bond wires or a spring clamp, the substrate sits within 1–5 mK of $T_{\mathrm{mc}}$. The qubit's coupling to phonons is weak (it contributes to $T_1$ only through dielectric loss, whose temperature dependence is flat below 100 mK), so this bath matters mainly through the chip's temperature-dependent resonator frequency shifts ($\delta f/f \sim 10^{-6}$ per 10 mK below 100 mK from kinetic inductance and TLS dispersion), which the VNA instrument reproduces (`12 §3`).
3. **Quasiparticle bath** — non-equilibrium quasiparticles from stray radiation and cosmic-ray/radioactive events; independent of $T_{\mathrm{mc}}$ below $\sim 150$ mK; a `Model` density $x_{\mathrm{qp}}$ set in the device file and reduced by the infrared filters and the shielding of §8.

The upshot, displayed in the chip inspector: the qubit's effective temperature is $\max(T_{\mathrm{line}}, T_{\mathrm{substrate}}, T_{\mathrm{qp}}^{\mathrm{eff}})$ weighted by the coupling rates, and is ordinarily 40–60 mK even with the MXC at 15 mK. Warming the MXC (by adding load in the model) does *not* change the qubit population until $T_{\mathrm{mc}}$ exceeds $T_{\mathrm{line}}$; at $T_{\mathrm{mc}} = 100$ mK the population rises to $\sim 6$ % at 5 GHz and $T_1$ falls through the thermal-photon and TLS channels — the behaviour the user sees when they inject a vacuum leak or run the still heater too hard.

## 10. What the simulator takes from this document

| Quantity | Computed by | Consumed by |
|----------|-------------|-------------|
| Stage temperatures $T_i(t)$ | (7.1) with (1.3), §2 map, §4–§5 loads | `11 §3` thermal network; fridge inspector; thermometer instruments (`12 §6`) |
| Per-line conduction and dissipation | (4.1), §4.2, §5 | `11 §5` wiring budget; line inspector (`17 §3.2`); port-capacity validator |
| Line photon temperature $T_{\mathrm{line}}$ | T07 (9.2) with $T_i$ from here | `08 §4`–`§5` thermal population, photon dephasing |
| Base temperature under load | (1.3)–(1.4) | fridge inspector; fault-injection responses (`11 §8`) |
| Cooldown timeline | (7.1)–(7.2) | lab "cooldown" mode timeline (`11 §6`); time estimator's *setup* term (`15 §3`) |
| Residual magnetic field | §8 shielding chain | `08 §4` flux-noise floor for tunable devices |
| Thermometer readings with self-heating | §8 | `12 §6` |

## Where this is used

| Result | Used by |
|--------|---------|
| (1.1)–(1.5) mixture, cooling power, exchangers | `11 §2` dilution unit model; `17 §3.1` mixing chamber, still, exchanger components |
| §2 pulse-tube capacity map | `11 §2`, `17 §3.1` pulse tube and compressor components |
| §3 stage table | `11 §3`, all stage components; nominal values in `Assets/Lab/Layouts/*/layout.json` |
| (4.1), §4 tables | `11 §5` wiring model; `Assets/Lab/Components/coax_*` spec sheets |
| (5.1), §5 loads | `11 §4` radiation and dissipation terms; HEMT/attenuator components |
| §6 budget | `11 §5` budget panel; `09 §7` wiring plan per device |
| (7.1)–(7.2) cooldown | `11 §6` cooldown simulation; `15 §3` setup time |
| §8 thermometry, vibration, shielding | `12 §6` thermometer instruments; `08 §4` flux-noise floor; `17 §3.1` shields |
| §9 qubit baths | `08 §5` initial state; chip inspector effective temperature (`17 §3.5`) |
