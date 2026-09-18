#!/usr/bin/env python3
"""Generates QuantumXLab content assets (spec 17 §3–4, §11; 20 §4; 16 §1–2; 22 §6; 19 §1; 12 §14).

Every file is wrapped in the core envelope (spec 04 §8):
  { "qxl": { "kind", "schema", "app", "created" }, "data": {...} }

Theory anchors use the form  T<nn>#<section-number>-<slug>  where the slug is derived from the
heading text: lowercase, '$...$' math stripped to its letters, non-alphanumerics -> '-',
runs collapsed, edges trimmed.  Example: "### 6.3 Dispersive readout" -> "T05#6.3-dispersive-readout".
Run:  python3 tools/gen_assets.py   (idempotent; validates at the end and exits non-zero on error)
"""
import json, os, re, sys, datetime, itertools

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "Assets")
APP = "0.1.0"
CREATED = datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
ERRORS = []

def envelope(kind, data, schema=1):
    return {"qxl": {"kind": kind, "schema": schema, "app": APP, "created": CREATED}, "data": data}

def write_json(rel, kind, data):
    path = os.path.join(ASSETS, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf8") as f:
        json.dump(envelope(kind, data), f, indent=2, ensure_ascii=False)
        f.write("\n")
    return path

def slugify(text):
    text = re.sub(r"\$([^$]*)\$", r"\1", text)          # keep math letters
    text = text.replace("\\", " ").lower()
    text = re.sub(r"[^a-z0-9]+", "-", text)
    return text.strip("-")

# ---------------------------------------------------------------- theory anchors
def theory_anchors():
    """Returns {doc: {anchor,...}} from docs/theory/T??-*.md headings."""
    out = {}
    tdir = os.path.join(ROOT, "docs", "theory")
    for fn in sorted(os.listdir(tdir)):
        m = re.match(r"(T\d\d)-", fn)
        if not m:
            continue
        doc = m.group(1)
        anchors = set()
        for line in open(os.path.join(tdir, fn), encoding="utf8"):
            h = re.match(r"^(#{2,3})\s+(\d+(?:\.\d+)?)\.?\s+(.*)$", line.rstrip())
            if h:
                anchors.add(f"{h.group(2)}-{slugify(h.group(3))}")
            elif line.startswith("## Where this is used"):
                anchors.add("where-this-is-used")
        out[doc] = anchors
    return out

ANCHORS = theory_anchors()

def anchor(doc, section):
    """Build 'Tnn#<section>-<slug>' for a section number by looking it up in the real headings."""
    for a in ANCHORS.get(doc, ()):
        if a.split("-", 1)[0] == section:
            return f"{doc}#{a}"
    ERRORS.append(f"no heading {doc} §{section}")
    return f"{doc}#{section}"

# ---------------------------------------------------------------- registries filled by later chunks
EQUATIONS = []      # dicts per spec 20 §4
ASSUMPTIONS = {}    # id -> one-line statement
GATES = []          # gates.json entries
COMPONENTS = []     # component descriptors
QEC_CODES = []      # (id, data)
RECIPES = []        # (id, data)

def eq(id, latex, plain, terms, assumptions, doc, section, cls="Exact"):
    EQUATIONS.append({"id": id, "latex": latex, "plain": plain,
                      "terms": [{"symbol": s, "name": n, "unit": u, **({"binding": b} if b else {})}
                                for (s, n, u, b) in terms],
                      "assumptions": assumptions, "theory": anchor(doc, section), "class": cls})
    return id

def assumption(id, text):
    ASSUMPTIONS[id] = text
    return id

# ================================================================ assumptions
for _id, _t in {
    "transmon_regime": "E_J/E_C ≫ 1 (typically 30–100): charge dispersion exponentially suppressed, weak anharmonicity.",
    "dispersive_regime": "|g/Δ| ≪ 1 and photon number well below n_crit = Δ²/4g².",
    "rwa": "Rotating-wave approximation: counter-rotating terms dropped (drive/coupling ≪ transition frequency).",
    "two_level": "Only the two lowest levels of each element are kept.",
    "three_level": "Transmon truncated to |0⟩,|1⟩,|2⟩; higher levels neglected.",
    "markov": "Bath correlation time much shorter than system time scales (Born–Markov, secular approximation).",
    "independent_errors": "Gate errors on different gates and qubits are statistically independent.",
    "average_gate_fidelities": "Each gate error is the average (Clifford-twirled) error rate from randomized benchmarking.",
    "no_crosstalk": "Classical drive crosstalk and ZZ during idles neglected unless listed in the calibration.",
    "calibration_static": "Calibration values are constant over the run (no drift).",
    "queue_time_excluded": "Wall time excludes any scheduler or queue wait on a shared machine.",
    "reset_policy_active": "Qubits are reset by measurement plus conditional X (active reset).",
    "reset_policy_passive": "Qubits relax for 5·T1 between shots (passive reset).",
    "lumped_thermal": "Each stage is one isothermal node with a single heat capacity.",
    "steady_state": "Time derivatives set to zero: loads balance cooling power.",
    "grey_body": "Radiative exchange between concentric shields with effective emissivity ε_eff = ε/(2−ε).",
    "cable_loss_cold_end": "Distributed coax loss is lumped at the cold end of each segment.",
    "phase_preserving_amp": "Amplifier is phase-preserving: at least half a photon of added noise (Caves bound).",
    "matched_filter": "Readout integration weights equal the difference of the mean |0⟩/|1⟩ trajectories.",
    "gaussian_iq": "IQ clouds are Gaussian with equal covariance for the two states.",
    "lamb_dicke": "η²(2n̄+1) ≪ 1: laser–ion coupling expanded to first order in the motion.",
    "single_mode_ms": "Mølmer–Sørensen gate drives one motional mode; spectator modes neglected.",
    "code_capacity": "Errors only on data qubits between perfect syndrome measurements.",
    "circuit_level_noise": "Every gate, idle, reset and measurement carries depolarizing/flip noise with probability p.",
    "surface_code_scaling": "p_L ≈ A (p/p_th)^((d+1)/2) with A = 0.1, p_th = 1 % (circuit-level).",
    "little_endian": "Qubit q[0] is the least significant bit of the basis-state index.",
    "pure_state": "The state is pure; for mixed states use the density-operator form.",
    "clifford_twirl": "The error channel is twirled to a depolarizing channel by random Cliffords.",
    "ideal_pulse_shape": "Sample-rate quantization and mixer imperfections neglected.",
}.items():
    assumption(_id, _t)

# ================================================================ equations — superconducting qubits (T05)
eq("lc_hamiltonian", r"H = \frac{Q^2}{2C} + \frac{\Phi^2}{2L},\qquad \hbar\omega = \frac{\hbar}{\sqrt{LC}}",
   "H = Q^2/(2C) + Phi^2/(2L); omega = 1/sqrt(L C)",
   [("Q", "Node charge", "C", None), (r"\Phi", "Node flux", "Wb", None), ("C", "Capacitance", "F", None), ("L", "Inductance", "H", None)],
   [], "T05", "1")
eq("josephson_relations", r"I = I_c \sin\varphi,\qquad V = \frac{\Phi_0}{2\pi}\dot\varphi,\qquad E_J = \frac{\Phi_0 I_c}{2\pi}",
   "I = Ic sin(phi); V = Phi0/(2 pi) dphi/dt; EJ = Phi0 Ic/(2 pi)",
   [("I_c", "Critical current", "nA", "device.qubit[$i].Ic"), (r"\varphi", "Phase difference", "rad", None), ("E_J", "Josephson energy (E/h)", "GHz", "device.qubit[$i].EJ")],
   [], "T05", "2")
eq("josephson_inductance", r"L_J = \frac{\Phi_0}{2\pi I_c\cos\varphi}",
   "LJ = Phi0/(2 pi Ic cos(phi))",
   [("L_J", "Josephson inductance", "nH", None), ("I_c", "Critical current", "nA", "device.qubit[$i].Ic")], [], "T05", "2")
eq("transmon_hamiltonian", r"H = 4E_C(\hat n - n_g)^2 - E_J\cos\hat\varphi,\qquad E_C = \frac{e^2}{2C_\Sigma}",
   "H = 4 EC (n - ng)^2 - EJ cos(phi); EC = e^2/(2 C_Sigma)",
   [("E_C", "Charging energy (E/h)", "GHz", "device.qubit[$i].EC"), ("E_J", "Josephson energy (E/h)", "GHz", "device.qubit[$i].EJ"),
    ("n_g", "Offset charge", "2e", None), (r"C_\Sigma", "Total shunt capacitance", "fF", None)],
   [], "T05", "3.1", "Numerical")
eq("transmon_frequency", r"\hbar\omega_{01} \simeq \sqrt{8E_JE_C} - E_C,\qquad \hbar\alpha \simeq -E_C",
   "f01 ~ sqrt(8 EJ EC) - EC; alpha ~ -EC",
   [(r"\omega_{01}", "Qubit transition frequency", "GHz", "device.qubit[$i].f01"), (r"\alpha", "Anharmonicity", "MHz", "device.qubit[$i].alpha"),
    ("E_J", "Josephson energy", "GHz", "device.qubit[$i].EJ"), ("E_C", "Charging energy", "GHz", "device.qubit[$i].EC")],
   ["transmon_regime"], "T05", "3.2", "Model")
eq("charge_dispersion", r"\epsilon_m \simeq (-1)^m E_C\,\frac{2^{4m+5}}{m!}\sqrt{\frac{2}{\pi}}\left(\frac{E_J}{2E_C}\right)^{\frac m2+\frac34} e^{-\sqrt{8E_J/E_C}}",
   "eps_m ~ (-1)^m EC 2^(4m+5)/m! sqrt(2/pi) (EJ/(2EC))^(m/2+3/4) exp(-sqrt(8 EJ/EC))",
   [(r"\epsilon_m", "Charge dispersion of level m", "kHz", None), ("E_J", "Josephson energy", "GHz", "device.qubit[$i].EJ"), ("E_C", "Charging energy", "GHz", "device.qubit[$i].EC")],
   ["transmon_regime"], "T05", "3.2", "Model")
eq("duffing", r"H = \hbar\omega\,a^\dagger a + \frac{\hbar\alpha}{2}\,a^\dagger a^\dagger a a",
   "H = hbar omega a†a + hbar alpha/2 a†a†aa",
   [(r"\omega", "Qubit frequency", "GHz", "device.qubit[$i].f01"), (r"\alpha", "Anharmonicity", "MHz", "device.qubit[$i].alpha")],
   ["three_level"], "T05", "3.4")
eq("flux_tunability", r"E_J(\Phi) = E_{J\Sigma}\left|\cos\frac{\pi\Phi}{\Phi_0}\right|\sqrt{1 + d^2\tan^2\frac{\pi\Phi}{\Phi_0}}",
   "EJ(Phi) = EJ_sum |cos(pi Phi/Phi0)| sqrt(1 + d^2 tan^2(pi Phi/Phi0))",
   [(r"\Phi", "External flux", "Φ0", "device.qubit[$i].phi_ext"), ("E_{J\\Sigma}", "Sum of junction energies", "GHz", None), ("d", "Junction asymmetry", "", None)],
   [], "T05", "4", "Model")
eq("flux_from_current", r"\Phi = M I", "Phi = M I",
   [(r"\Phi", "Flux through the SQUID loop", "Φ0", "device.qubit[$i].phi_ext"), ("M", "Mutual inductance", "pH", None), ("I", "Flux-line current", "mA", "instr.dc.ch[$k].I")],
   [], "T05", "4", "Model")
eq("capacitive_coupling", r"H_{\rm int} = \hbar g\,(a^\dagger b + a b^\dagger),\qquad g \approx \frac{1}{2}\frac{C_g}{\sqrt{C_1 C_2}}\sqrt{\omega_1\omega_2}",
   "H_int = hbar g (a†b + ab†); g ~ Cg/(2 sqrt(C1 C2)) sqrt(omega1 omega2)",
   [("g", "Exchange coupling", "MHz", "device.edge[$e].J"), ("C_g", "Coupling capacitance", "fF", None)], ["rwa"], "T05", "5.1", "Model")
eq("tunable_coupler", r"\tilde g = g_{12} + \frac{g_{1c}g_{2c}}{2}\left(\frac{1}{\Delta_1} + \frac{1}{\Delta_2}\right)",
   "g_eff = g12 + g1c g2c/2 (1/Delta1 + 1/Delta2)",
   [(r"\tilde g", "Effective coupling", "MHz", "device.edge[$e].g_eff"), ("g_{1c}", "Qubit-1–coupler coupling", "MHz", None), ("g_{2c}", "Qubit-2–coupler coupling", "MHz", None),
    (r"\Delta_1", "ω1 − ωc", "MHz", None), (r"\Delta_2", "ω2 − ωc", "MHz", None)],
   ["dispersive_regime"], "T05", "5.2", "Model")
eq("zz_interaction", r"H_{ZZ} = \frac{\hbar\zeta}{4}\,Z\otimes Z,\qquad \zeta = \frac{2g^2(\alpha_1+\alpha_2)}{(\Delta_{12}+\alpha_1)(\Delta_{12}-\alpha_2)}",
   "H_ZZ = hbar zeta/4 ZZ; zeta = 2 g^2 (alpha1+alpha2)/((Delta12+alpha1)(Delta12-alpha2))",
   [(r"\zeta", "ZZ rate", "kHz", "device.edge[$e].zz"), ("g", "Exchange coupling", "MHz", "device.edge[$e].J"), (r"\Delta_{12}", "ω1 − ω2", "MHz", None),
    (r"\alpha_1", "Anharmonicity 1", "MHz", None), (r"\alpha_2", "Anharmonicity 2", "MHz", None)],
   ["dispersive_regime"], "T05", "5.4", "Model")
eq("jaynes_cummings", r"H = \hbar\omega_r a^\dagger a - \frac{\hbar\omega_q}{2}\sigma_z + \hbar g\,(a^\dagger\sigma^- + a\sigma^+)",
   "H = hbar omega_r a†a - hbar omega_q/2 sigma_z + hbar g (a† sigma- + a sigma+)",
   [(r"\omega_r", "Resonator frequency", "GHz", "device.res[$i].f_r"), (r"\omega_q", "Qubit frequency", "GHz", "device.qubit[$i].f01"), ("g", "Qubit–resonator coupling", "MHz", "device.res[$i].g")],
   ["rwa", "two_level"], "T05", "6.1")
eq("dispersive_shift", r"\chi = \frac{g^2}{\Delta}\frac{\alpha}{\Delta+\alpha}",
   "chi = g^2/Delta * alpha/(Delta+alpha)",
   [(r"\chi", "Dispersive shift", "MHz", "device.res[$i].chi"), ("g", "Qubit–resonator coupling", "MHz", "device.res[$i].g"),
    (r"\Delta", "Qubit–resonator detuning ω_q − ω_r", "MHz", "device.res[$i].Delta"), (r"\alpha", "Transmon anharmonicity", "MHz", "device.qubit[$i].alpha")],
   ["dispersive_regime"], "T05", "6.2", "Model")
eq("dispersive_hamiltonian", r"H_{\rm disp} = \hbar(\omega_r - \chi\sigma_z)\,a^\dagger a - \frac{\hbar}{2}(\omega_q+\chi)\sigma_z",
   "H = hbar (omega_r - chi sigma_z) a†a - hbar/2 (omega_q + chi) sigma_z",
   [(r"\chi", "Dispersive shift", "MHz", "device.res[$i].chi"), (r"\omega_r", "Resonator frequency", "GHz", "device.res[$i].f_r")],
   ["dispersive_regime"], "T05", "6.2")
eq("critical_photon_number", r"n_{\rm crit} = \frac{\Delta^2}{4g^2}", "n_crit = Delta^2/(4 g^2)",
   [(r"n_{\rm crit}", "Critical photon number", "", "device.res[$i].n_crit"), (r"\Delta", "Detuning", "MHz", "device.res[$i].Delta"), ("g", "Coupling", "MHz", "device.res[$i].g")],
   ["dispersive_regime"], "T05", "6.2", "Model")
eq("readout_optimum", r"2\chi = \kappa", "2 chi = kappa",
   [(r"\chi", "Dispersive shift", "MHz", "device.res[$i].chi"), (r"\kappa", "Resonator linewidth", "MHz", "device.res[$i].kappa")], [], "T05", "6.3", "Model")
eq("purcell_decay", r"\gamma_P = \kappa\frac{g^2}{\Delta^2}", "gamma_P = kappa g^2/Delta^2",
   [(r"\gamma_P", "Purcell decay rate", "kHz", "device.res[$i].gamma_P"), (r"\kappa", "Resonator linewidth", "MHz", "device.res[$i].kappa"),
    ("g", "Coupling", "MHz", "device.res[$i].g"), (r"\Delta", "Detuning", "MHz", "device.res[$i].Delta")], ["dispersive_regime"], "T05", "6.4", "Model")
eq("resonator_length", r"L = \frac{c}{4 f_r\sqrt{\varepsilon_{\rm eff}}}", "L = c/(4 f_r sqrt(eps_eff))",
   [("L", "Electrical length of the λ/4 resonator", "mm", None), ("f_r", "Resonance frequency", "GHz", "device.res[$i].f_r"), (r"\varepsilon_{\rm eff}", "Effective permittivity", "", None)],
   [], "T05", "6.3", "Model")
eq("drive_hamiltonian", r"H_d = \frac{\hbar\Omega(t)}{2}\left(\cos\phi\,\sigma_x + \sin\phi\,\sigma_y\right)", "H_d = hbar Omega/2 (cos(phi) sigma_x + sin(phi) sigma_y)",
   [(r"\Omega", "Rabi frequency", "MHz", "device.qubit[$i].drive_amp"), (r"\phi", "Drive phase", "rad", None)], ["rwa", "two_level"], "T05", "7")
eq("drag", r"\Omega_y(t) = -\frac{\dot\Omega_x(t)}{\alpha}", "Omega_y = -dOmega_x/dt / alpha",
   [(r"\Omega_y", "Quadrature (DRAG) envelope", "MHz", None), (r"\alpha", "Anharmonicity", "MHz", "device.qubit[$i].alpha")], ["three_level"], "T05", "7", "Model")
eq("cross_resonance", r"H_{CR} = \frac{\hbar}{2}\left(\nu_{ZX}\,ZX + \nu_{ZI}\,ZI + \nu_{IX}\,IX + \ldots\right),\qquad \nu_{ZX} = -\frac{J\Omega}{\Delta_{ct}}\frac{\alpha_c}{\Delta_{ct}+\alpha_c}",
   "H_CR = hbar/2 (nu_ZX ZX + nu_ZI ZI + nu_IX IX + ...); nu_ZX = -J Omega/Delta_ct * alpha_c/(Delta_ct+alpha_c)",
   [(r"\nu_{ZX}", "ZX rate", "MHz", "device.edge[$e].nu_zx"), ("J", "Exchange coupling", "MHz", "device.edge[$e].J"), (r"\Omega", "Drive amplitude on control", "MHz", None),
    (r"\Delta_{ct}", "ω_c − ω_t", "MHz", None), (r"\alpha_c", "Control anharmonicity", "MHz", None)], ["dispersive_regime", "rwa"], "T05", "8", "Model")
eq("cz_phase", r"\phi_{ZZ} = \int\left(\omega_{11} - \omega_{01} - \omega_{10}\right)dt = \pi", "phi_ZZ = ∫ (omega_11 - omega_01 - omega_10) dt = pi",
   [(r"\phi_{ZZ}", "Conditional phase", "rad", None)], [], "T05", "9", "Numerical")
eq("frequency_collisions", r"|f_i - f_j| \ge \delta_1,\quad |f_i + \tfrac{\alpha_i}{2} - f_j| \ge \delta_2,\quad |2f_i + \alpha_i - 2f_j| \ge \delta_3",
   "|fi - fj| >= d1; |fi + ai/2 - fj| >= d2; |2fi + ai - 2fj| >= d3",
   [("f_i", "Qubit frequency", "GHz", "device.qubit[$i].f01"), (r"\alpha_i", "Anharmonicity", "MHz", "device.qubit[$i].alpha")], [], "T05", "12", "Model")

# ================================================================ equations — open systems (T04)
eq("lindblad", r"\dot\rho = -\frac{i}{\hbar}[H,\rho] + \sum_k\left(L_k\rho L_k^\dagger - \tfrac12\{L_k^\dagger L_k,\rho\}\right)",
   "drho/dt = -i/hbar [H, rho] + sum_k (L_k rho L_k† - 1/2 {L_k† L_k, rho})",
   [(r"\rho", "Density operator", "", None), ("H", "System Hamiltonian", "J", None), ("L_k", "Collapse (jump) operators", "√Hz", None)],
   ["markov"], "T04", "2.1", "Numerical")
eq("amplitude_damping_kraus", r"K_0 = \begin{pmatrix}1 & 0\\ 0 & \sqrt{1-\gamma}\end{pmatrix},\quad K_1 = \begin{pmatrix}0 & \sqrt{\gamma}\\ 0 & 0\end{pmatrix},\quad \gamma = 1 - e^{-t/T_1}",
   "K0 = [[1,0],[0,sqrt(1-gamma)]]; K1 = [[0,sqrt(gamma)],[0,0]]; gamma = 1 - exp(-t/T1)",
   [(r"\gamma", "Decay probability", "", None), ("T_1", "Energy relaxation time", "µs", "device.qubit[$i].T1"), ("t", "Elapsed time", "ns", None)],
   ["markov"], "T04", "3.1")
eq("thermal_rates", r"\gamma_\uparrow = \gamma\,n_{th},\qquad \gamma_\downarrow = \gamma\,(n_{th}+1),\qquad p_1^{ss} = \frac{n_{th}}{2n_{th}+1}",
   "gamma_up = gamma n_th; gamma_down = gamma (n_th+1); p1_ss = n_th/(2 n_th + 1)",
   [(r"n_{th}", "Thermal photon number at the qubit frequency", "", "device.qubit[$i].n_th"), ("p_1^{ss}", "Steady-state excited population", "", "device.qubit[$i].pop_e")],
   ["markov"], "T04", "3.2", "Model")
eq("phase_damping_kraus", r"K_0 = \sqrt{1-\lambda/2}\,I,\quad K_1 = \sqrt{\lambda/2}\,Z,\qquad \lambda = 1 - e^{-2t/T_\varphi}",
   "K0 = sqrt(1-lambda/2) I; K1 = sqrt(lambda/2) Z; lambda = 1 - exp(-2t/T_phi)",
   [(r"T_\varphi", "Pure dephasing time", "µs", "device.qubit[$i].Tphi"), (r"\lambda", "Dephasing parameter", "", None)], ["markov"], "T04", "4.1")
eq("t2_relation", r"\frac{1}{T_2} = \frac{1}{2T_1} + \frac{1}{T_\varphi}", "1/T2 = 1/(2 T1) + 1/T_phi",
   [("T_2", "Coherence time", "µs", "device.qubit[$i].T2"), ("T_1", "Relaxation time", "µs", "device.qubit[$i].T1"), (r"T_\varphi", "Pure dephasing time", "µs", "device.qubit[$i].Tphi")],
   ["markov"], "T04", "4.2")
eq("thermal_relaxation_fidelity", r"F_{\rm avg}^{\rm therm}(\tau) = \frac12 + \frac{2e^{-\tau/T_2} + e^{-\tau/T_1}}{6}",
   "F_avg = 1/2 + (2 exp(-tau/T2) + exp(-tau/T1))/6",
   [(r"\tau", "Gate or idle duration", "ns", None), ("T_1", "Relaxation time", "µs", "device.qubit[$i].T1"), ("T_2", "Coherence time", "µs", "device.qubit[$i].T2")],
   ["markov"], "T04", "4.3", "Model")
eq("depolarizing", r"\mathcal E(\rho) = (1-p)\rho + p\,\frac{I}{d}", "E(rho) = (1-p) rho + p I/d",
   [("p", "Depolarizing probability", "", None), ("d", "Hilbert-space dimension", "", None)], [], "T04", "5.1")
eq("avg_fidelity_depolarizing", r"\bar F = 1 - p\,\frac{d-1}{d}\quad\Longleftrightarrow\quad p = \frac{d}{d-1}\,r,\qquad r = 1-\bar F",
   "F_avg = 1 - p (d-1)/d;  p = d/(d-1) r",
   [(r"\bar F", "Average gate fidelity", "", None), ("r", "Average error rate (RB)", "", "device.qubit[$i].error_1q"), ("p", "Depolarizing probability", "", None)],
   ["clifford_twirl"], "T04", "5.2")
eq("mcwf_jump", r"\delta p_k = \delta t\,\langle\psi|L_k^\dagger L_k|\psi\rangle,\qquad |\psi\rangle \to \frac{L_k|\psi\rangle}{\sqrt{\delta p_k/\delta t}}",
   "dp_k = dt <psi|L_k† L_k|psi>; on jump psi -> L_k psi / norm",
   [(r"\delta p_k", "Jump probability in one step", "", None), ("L_k", "Jump operator", "√Hz", None)], ["markov"], "T04", "7", "Statistical")
eq("ramsey_decay", r"P_1(t) = \frac12\left[1 + e^{-t/T_2^*}\cos(2\pi\delta t + \phi)\right]", "P1 = 1/2 (1 + exp(-t/T2*) cos(2 pi delta t + phi))",
   [("T_2^*", "Ramsey dephasing time", "µs", "device.qubit[$i].T2star"), (r"\delta", "Detuning", "MHz", None)], [], "T04", "8.1", "Model")
eq("zz_phase", r"U_{ZZ}(t) = e^{-i\zeta t\,ZZ/4}", "U = exp(-i zeta t ZZ/4)",
   [(r"\zeta", "ZZ rate", "kHz", "device.edge[$e].zz"), ("t", "Idle time", "ns", None)], [], "T04", "9.1")
eq("readout_confusion", r"\vec p_{\rm meas} = M^{\!\top}\vec p_{\rm true},\qquad M_{ij} = P(\text{read } j\mid\text{prepared } i)",
   "p_meas = M^T p_true; M_ij = P(read j | prepared i)",
   [("M", "Assignment matrix", "", "device.qubit[$i].assignment"), (r"\vec p", "Outcome probabilities", "", None)], [], "T04", "10.1", "Model")
eq("measurement_dephasing", r"\Gamma_m = \frac{8\chi^2\bar n}{\kappa}\quad(\chi\ll\kappa)", "Gamma_m = 8 chi^2 n / kappa",
   [(r"\Gamma_m", "Measurement-induced dephasing rate", "MHz", None), (r"\bar n", "Resonator photon number", "", "device.res[$i].n_photons"),
    (r"\chi", "Dispersive shift", "MHz", "device.res[$i].chi"), (r"\kappa", "Resonator linewidth", "MHz", "device.res[$i].kappa")], ["dispersive_regime"], "T04", "10.4", "Model")

# ================================================================ equations — control & signals (T07)
eq("rotating_frame", r"H' = U H U^\dagger + i\hbar\,\dot U U^\dagger,\qquad U = e^{i\omega_d t\,\sigma_z/2}", "H' = U H U† + i hbar dU/dt U†",
   [(r"\omega_d", "Drive (frame) frequency", "GHz", None)], [], "T07", "1")
eq("rabi_generalized", r"P_1(t) = \frac{\Omega^2}{\Omega^2+\delta^2}\sin^2\!\left(\frac{\sqrt{\Omega^2+\delta^2}}{2}t\right)", "P1 = Omega^2/(Omega^2+delta^2) sin^2(sqrt(Omega^2+delta^2) t/2)",
   [(r"\Omega", "Rabi frequency", "MHz", "device.qubit[$i].drive_amp"), (r"\delta", "Detuning ω_d − ω_q", "MHz", None)], ["rwa", "two_level"], "T07", "2")
eq("rabi_from_power", r"V = \sqrt{2 Z_0 P},\qquad \hbar\Omega = 2\beta e V\,|\langle 0|\hat n|1\rangle|", "V = sqrt(2 Z0 P); hbar Omega = 2 beta e V |<0|n|1>|",
   [("P", "Power at the chip", "dBm", "wiring.line[$line].P_chip"), ("Z_0", "Line impedance", "Ω", None), (r"\beta", "Drive-line coupling ratio C_d/C_Σ", "", None),
    (r"\Omega", "Rabi frequency", "MHz", "device.qubit[$i].drive_amp")], ["rwa"], "T07", "3", "Model")
eq("area_theorem", r"\theta = \int_0^{\tau}\Omega(t)\,dt", "theta = ∫ Omega dt",
   [(r"\theta", "Rotation angle", "rad", None), (r"\Omega", "Rabi envelope", "MHz", None), (r"\tau", "Pulse duration", "ns", None)], ["rwa", "two_level"], "T07", "4")
eq("dbm_to_watts", r"P[\mathrm W] = 10^{(P[\mathrm{dBm}]-30)/10}", "P_W = 10^((P_dBm - 30)/10)", [("P", "Power", "dBm", "instr.gen[$i].P_dBm")], [], "T07", "3")
eq("iq_modulation", r"s(t) = I(t)\cos\omega_{LO}t - Q(t)\sin\omega_{LO}t", "s = I cos(w_LO t) - Q sin(w_LO t)",
   [("I", "In-phase envelope", "V", "instr.awg.ch[$k].waveform"), ("Q", "Quadrature envelope", "V", None), (r"\omega_{LO}", "Local-oscillator frequency", "GHz", "instr.gen[$i].f")], ["ideal_pulse_shape"], "T07", "5")
eq("heterodyne_demod", r"I + iQ = \frac{1}{T}\int_0^{T} s(t)\,w(t)\,e^{-i\omega_{IF}t}\,dt", "I+iQ = 1/T ∫ s(t) w(t) exp(-i w_IF t) dt",
   [(r"\omega_{IF}", "Intermediate frequency", "MHz", None), ("w", "Integration weights", "", None), ("T", "Integration time", "µs", None)], ["matched_filter"], "T07", "6")
eq("readout_snr", r"\mathrm{SNR} = |\alpha_0 - \alpha_1|\sqrt{2\eta\kappa\tau},\qquad P_{\rm err} = \tfrac12\,\mathrm{erfc}\!\left(\frac{\mathrm{SNR}}{2\sqrt2}\right)",
   "SNR = |a0 - a1| sqrt(2 eta kappa tau); P_err = 1/2 erfc(SNR/(2 sqrt 2))",
   [(r"\alpha_0", "Pointer amplitude for |0⟩", "", None), (r"\alpha_1", "Pointer amplitude for |1⟩", "", None), (r"\eta", "Measurement efficiency", "", "wiring.readout.eta"), (r"\kappa", "Resonator linewidth", "MHz", "device.res[$i].kappa"), (r"\tau", "Integration time", "µs", None)],
   ["gaussian_iq", "matched_filter"], "T07", "6", "Model")
eq("adc_snr", r"\mathrm{SNR}_{\rm q} \approx 6.02\,N + 1.76\ \mathrm{dB}", "SNR = 6.02 N + 1.76 dB", [("N", "Converter bits", "", None)], [], "T07", "7", "Model")
eq("friis", r"T_{\rm sys} = T_1 + \frac{T_2}{G_1} + \frac{T_3}{G_1G_2} + \cdots", "T_sys = T1 + T2/G1 + T3/(G1 G2) + ...",
   [(r"T_{\rm sys}", "System noise temperature", "K", "wiring.line[$line].T_sys"), ("T_1", "Noise temperature of the first stage", "K", None), ("G_1", "Gain of the first stage", "", None)], [], "T07", "8", "Model")
eq("quantum_limit", r"T_N^{\min} = \frac{\hbar\omega}{2k_B}\quad(\text{phase-preserving})", "T_N,min = hbar omega/(2 k_B)",
   [(r"T_N", "Amplifier noise temperature", "K", "wiring.line[$line].jpa.T_N"), (r"\omega", "Signal frequency", "GHz", None)], ["phase_preserving_amp"], "T07", "8")
eq("thermal_photon_number", r"n_{th}(f,T) = \frac{1}{e^{hf/k_BT} - 1}", "n = 1/(exp(h f/(k_B T)) - 1)",
   [("f", "Frequency", "GHz", None), ("T", "Temperature", "K", "cryo.stage.$stage.T")], [], "T07", "9")
eq("attn_noise_cascade", r"n_{\rm out} = \frac{n_{\rm in}}{A} + \left(1 - \frac1A\right) n_{th}(f, T_s)", "n_out = n_in/A + (1 - 1/A) n_th(f, T_s)",
   [("A", "Linear attenuation 10^{A_dB/10}", "", "static.A_dB"), ("T_s", "Stage temperature", "K", "cryo.stage.$stage.T"), (r"n_{\rm out}", "Output photon number", "", "wiring.line[$line].attn[$k].n_th")],
   ["cable_loss_cold_end"], "T07", "9", "Model")
eq("attn_dissipation", r"P_{\rm diss} = P_{\rm in}\left(1 - 10^{-A/10}\right)", "P_diss = P_in (1 - 10^(-A/10))",
   [(r"P_{\rm in}", "Input power", "W", None), ("A", "Attenuation", "dB", "static.A_dB"), (r"P_{\rm diss}", "Dissipated power", "W", "wiring.line[$line].attn[$k].P_diss")], [], "T07", "9", "Model")
eq("effective_temperature", r"T_{\rm eff} = \frac{hf}{k_B\ln(1 + 1/n)}", "T_eff = h f/(k_B ln(1 + 1/n))", [("n", "Photon number", "", None), ("f", "Frequency", "GHz", None)], [], "T07", "9")
eq("thermal_population", r"P_1 = \frac{1}{1 + e^{hf/k_BT_{\rm eff}}}", "P1 = 1/(1 + exp(h f/(k_B T_eff)))",
   [("P_1", "Excited-state population", "", "device.qubit[$i].pop_e"), (r"T_{\rm eff}", "Effective qubit temperature", "mK", "device.qubit[$i].T_eff")], [], "T07", "10", "Model")
eq("lpf_cutoff", r"f_c = \frac{1}{2\pi RC}", "f_c = 1/(2 pi R C)", [("f_c", "Cutoff frequency", "MHz", "static.f_c"), ("R", "Resistance", "Ω", None), ("C", "Capacitance", "nF", None)], [], "T07", "11", "Model")

# ================================================================ equations — cryogenics (T08)
eq("cooling_power_mxc", r"\dot Q_{\rm mc} = 84\,\dot n_3\,T_{\rm mc}^2 - \dot Q_0", "Q_mc = 84 n3 T_mc^2 - Q0",
   [(r"\dot Q_{\rm mc}", "Mixing-chamber cooling power", "µW", "cryo.stage.mxc.P_cool"), (r"\dot n_3", "³He circulation rate", "mmol/s", "cryo.flow.n3"), (r"T_{\rm mc}", "Mixing-chamber temperature", "mK", "cryo.stage.mxc.T")],
   ["steady_state"], "T08", "1.2", "Model")
eq("cooling_power_general", r"\dot Q = \dot n_3\left(95\,T_{\rm mc}^2 - 11\,T_{\rm ex}^2\right)", "Q = n3 (95 T_mc^2 - 11 T_ex^2)",
   [(r"T_{\rm ex}", "Temperature of ³He entering the mixing chamber", "mK", "cryo.hx.step[3].T"), (r"\dot n_3", "Circulation rate", "mmol/s", "cryo.flow.n3")], [], "T08", "1.2", "Model")
eq("still_flow", r"\dot n_3 \approx \frac{\dot Q_{\rm still}}{L_3(T_{\rm still})}", "n3 ~ Q_still / L3(T_still)",
   [(r"\dot Q_{\rm still}", "Still heater power", "mW", "cryo.stage.still.P_heater"), ("L_3", "Latent heat of ³He", "J/mol", None)], [], "T08", "1.3", "Model")
eq("pt_capacity", r"\dot Q_{\rm PT2}(T) \approx 1.5\,\mathrm W \cdot \frac{T - 2.8\,\mathrm K}{4.2\,\mathrm K - 2.8\,\mathrm K}", "Q_PT2 ~ 1.5 W (T - 2.8 K)/(1.4 K)",
   [(r"\dot Q_{\rm PT2}", "Second-stage cooling power", "W", "cryo.stage.s4.P_cool"), ("T", "Second-stage temperature", "K", "cryo.pt.stage2.T")], ["lumped_thermal"], "T08", "2", "Model")
eq("conduction_load", r"\dot Q = \frac{A}{L}\int_{T_c}^{T_h} k(T)\,dT", "Q = A/L ∫ k(T) dT",
   [("A", "Conductor cross-section", "mm²", None), ("L", "Segment length", "m", "static.length_m"), ("k", "Thermal conductivity", "W/(m·K)", None), (r"\dot Q", "Conducted heat", "W", "wiring.line[$line].seg[$j].P_cond")],
   ["steady_state"], "T08", "4", "Model")
eq("radiation_load", r"\dot Q = \sigma A\,\varepsilon_{\rm eff}\left(T_h^4 - T_c^4\right),\qquad \varepsilon_{\rm eff} = \frac{\varepsilon}{2-\varepsilon}", "Q = sigma A eps_eff (Th^4 - Tc^4)",
   [(r"\sigma", "Stefan–Boltzmann constant", "W/(m²·K⁴)", None), ("A", "Shield area", "m²", None), (r"\varepsilon", "Emissivity", "", None), ("T_h", "Warmer shield temperature", "K", None)],
   ["grey_body"], "T08", "5", "Model")
eq("cooldown_lumped", r"C(T)\,\dot T = \dot Q_{\rm cool}(T) - \dot Q_{\rm load}(T)", "C(T) dT/dt = Q_cool - Q_load",
   [("C", "Stage heat capacity", "J/K", None), (r"\dot Q_{\rm cool}", "Cooling power", "W", "cryo.stage.$stage.P_cool"), (r"\dot Q_{\rm load}", "Heat load", "W", "cryo.stage.$stage.P_load")],
   ["lumped_thermal"], "T08", "7", "Numerical")
eq("debye_heat_capacity", r"C_V = 9Nk_B\left(\frac{T}{\Theta_D}\right)^3\int_0^{\Theta_D/T}\frac{x^4e^x}{(e^x-1)^2}dx", "Debye C_V",
   [(r"\Theta_D", "Debye temperature", "K", None)], [], "T08", "7", "Model")
eq("kapitza", r"R_K \propto T^{-3}", "R_K ~ T^-3", [("R_K", "Kapitza boundary resistance", "K/W", None)], [], "T08", "1.3", "Model")
eq("ruo2_thermometer", r"R(T) = R_0\exp\left[(T_0/T)^{1/4}\right]", "R = R0 exp((T0/T)^(1/4))",
   [("R", "Sensor resistance", "kΩ", "cryo.thermo[$i].R"), ("T", "Temperature", "mK", "cryo.stage.$stage.T")], [], "T08", "8", "Model")
eq("turbo_pressure", r"p(t) = p_{\rm base} + (p_0 - p_{\rm base})e^{-S t/V}", "p = p_base + (p0 - p_base) exp(-S t/V)",
   [("S", "Pumping speed", "L/s", None), ("V", "Pumped volume", "L", None), ("p", "OVC pressure", "mbar", "cryo.ovc.pressure")], [], "T08", "1.3", "Model")

# ================================================================ equations — trapped ions (T06)
eq("paul_pseudopotential", r"\Phi_{ps} = \frac{q^2V_{RF}^2}{4m\Omega_{RF}^2r_0^4}\,r^2", "Phi_ps = q^2 V_RF^2/(4 m Omega_RF^2 r0^4) r^2",
   [(r"V_{RF}", "RF amplitude", "V", "instr.rf.V"), (r"\Omega_{RF}", "RF drive frequency", "MHz", None), ("r_0", "Electrode–axis distance", "µm", None)], [], "T06", "1.1", "Model")
eq("chain_equilibrium", r"u_m - \sum_{n\ne m}\frac{\mathrm{sgn}(m-n)}{(u_m-u_n)^2} = 0,\qquad \ell^3 = \frac{e^2}{4\pi\varepsilon_0 m\omega_z^2}", "u_m - sum sgn(m-n)/(u_m-u_n)^2 = 0",
   [(r"\omega_z", "Axial COM frequency", "MHz", "device.trap.omega_z"), (r"\ell", "Length scale", "µm", None)], [], "T06", "1.2", "Numerical")
eq("normal_modes", r"\omega_p = \omega_z\sqrt{\mu_p},\qquad \omega_{\rm COM} = \omega_z,\quad \omega_{\rm br} = \sqrt3\,\omega_z", "omega_p = omega_z sqrt(mu_p)",
   [(r"\mu_p", "Hessian eigenvalue", "", None), (r"\omega_z", "Axial frequency", "MHz", "device.trap.omega_z")], [], "T06", "1.3", "Numerical")
eq("lamb_dicke", r"\eta = k\cos\theta\sqrt{\frac{\hbar}{2m\omega}}", "eta = k cos(theta) sqrt(hbar/(2 m omega))",
   [(r"\eta", "Lamb–Dicke parameter", "", "device.trap.eta"), ("k", "Laser wavenumber", "1/µm", None), (r"\omega", "Mode frequency", "MHz", None)], ["lamb_dicke"], "T06", "3")
eq("ms_hamiltonian", r"H_{MS} = \hbar g\,S_\phi\left(a e^{-i\delta t} + a^\dagger e^{i\delta t}\right),\qquad g = \frac{\eta\Omega}{2}", "H_MS = hbar g S_phi (a e^{-i delta t} + a† e^{i delta t})",
   [("g", "Spin–motion coupling", "kHz", None), (r"\delta", "Symmetric detuning from the sideband", "kHz", None), (r"\Omega", "Carrier Rabi frequency", "MHz", None)], ["lamb_dicke", "single_mode_ms"], "T06", "6.1")
eq("ms_closure", r"\tau = \frac{2\pi K}{\delta},\qquad \Phi(\tau) = \frac{g^2\tau}{\delta} = \frac{\pi}{8}\ \Rightarrow\ XX(\pi/4)", "tau = 2 pi K/delta; Phi = g^2 tau/delta",
   [(r"\tau", "Gate duration", "µs", "device.edge[$e].t_ms"), ("K", "Number of phase-space loops", "", None)], ["single_mode_ms"], "T06", "6.2", "Model")
eq("fluorescence_threshold", r"P(n\ge n_{\rm th}\mid\text{dark}) = 1 - \sum_{n<n_{\rm th}}\frac{\lambda_d^n e^{-\lambda_d}}{n!}", "Poisson threshold error",
   [(r"\lambda_d", "Mean dark counts", "", None), (r"n_{\rm th}", "Threshold count", "", None)], [], "T06", "7.1", "Statistical")
eq("heating_rate", r"\bar n(t) = \bar n_0 + \dot{\bar n}\,t", "n(t) = n0 + ndot t", [(r"\dot{\bar n}", "Motional heating rate", "quanta/s", "device.trap.heating")], [], "T06", "8", "Model")

# ================================================================ equations — QEC, benchmarking, estimation, states
eq("stabilizer_condition", r"S|\psi\rangle = |\psi\rangle\ \forall S\in\mathcal S,\qquad E\text{ detectable}\iff \exists S: \{E,S\}=0", "S psi = psi for all S in stabilizer; E detectable iff anticommutes with some S",
   [("S", "Stabilizer generator", "", None), ("E", "Error operator", "", None)], [], "T09", "2.3")
eq("surface_logical_error", r"p_L \approx A\left(\frac{p}{p_{th}}\right)^{(d+1)/2},\qquad A = 0.1,\ p_{th} = 10^{-2}", "p_L ~ A (p/p_th)^((d+1)/2)",
   [("p_L", "Logical error per cycle", "", None), ("p", "Physical error rate", "", "device.error_2q_mean"), ("d", "Code distance", "", None)], ["surface_code_scaling", "circuit_level_noise"], "T09", "7", "Model")
eq("surface_qubits", r"n_{\rm phys} = 2d^2 - 1\ \text{per logical qubit}", "n = 2 d^2 - 1", [("d", "Code distance", "", None)], [], "T09", "5.1")
eq("distillation_15to1", r"p_{\rm out} \approx 35\,p_{\rm in}^3", "p_out ~ 35 p_in^3", [(r"p_{\rm in}", "Input magic-state error", "", None)], [], "T09", "8.3", "Model")
eq("qec_resources", r"N_{\rm phys} = Q_L(2d^2-1)\cdot 1.5 + N_{\rm fact},\qquad T = N_T\cdot d\,t_c", "N_phys = Q_L (2d^2-1) 1.5 + N_fact; T = N_T d t_c",
   [("Q_L", "Logical qubits", "", None), ("N_T", "T count", "", None), ("t_c", "Syndrome cycle time", "µs", "static.qec.cycle_time")], ["surface_code_scaling"], "T09", "9", "Model")
eq("avg_fidelity_from_entanglement", r"\bar F = \frac{dF_e + 1}{d+1}", "F_avg = (d F_e + 1)/(d+1)", [("F_e", "Entanglement fidelity", "", None), ("d", "Dimension", "", None)], [], "T10", "1")
eq("rb_decay", r"F(m) = A\,p^m + B,\qquad r = (1-p)\frac{d-1}{d}", "F(m) = A p^m + B; r = (1-p)(d-1)/d",
   [("m", "Sequence length (Cliffords)", "", None), ("p", "Depolarizing parameter", "", None), ("r", "Error per Clifford", "", "device.qubit[$i].error_1q")], ["clifford_twirl"], "T10", "2", "Statistical")
eq("interleaved_rb", r"r_{\rm gate} = \left(1 - \frac{p_{\rm int}}{p_{\rm ref}}\right)\frac{d-1}{d}", "r_gate = (1 - p_int/p_ref)(d-1)/d",
   [(r"p_{\rm int}", "Interleaved decay", "", None), (r"p_{\rm ref}", "Reference decay", "", None)], ["clifford_twirl"], "T10", "2", "Statistical")
eq("state_tomography_inversion", r"\rho = \frac{1}{2^n}\sum_{P}\langle P\rangle\,P", "rho = 2^-n sum_P <P> P", [("P", "Pauli string", "", None)], [], "T10", "3", "Statistical")
eq("xeb", r"F_{\rm XEB} = 2^n\langle p(x)\rangle_{\rm samples} - 1", "F_XEB = 2^n <p(x)> - 1", [("p(x)", "Ideal probability of sampled bitstring", "", None)], [], "T10", "5", "Statistical")
eq("wall_time", r"T_{\rm run} = T_{\rm load} + N_{\rm shots}\left(T_{\rm reset} + T_{\rm circ} + T_{\rm ro} + T_{\rm gap}\right)", "T_run = T_load + N (T_reset + T_circ + T_ro + T_gap)",
   [(r"T_{\rm circ}", "Scheduled critical path", "µs", "run.T_circ"), (r"T_{\rm ro}", "Readout duration", "µs", None), (r"T_{\rm gap}", "Repetition overhead", "µs", None), (r"N_{\rm shots}", "Shots", "", "run.shots")],
   ["queue_time_excluded", "calibration_static"], "T12", "1", "Model")
eq("fidelity_estimate", r"F_{\rm est} \approx \prod_g (1-\epsilon_g)\prod_{q}\prod_{\rm idle}\left[1 - \epsilon_{\rm idle}(t)\right]\prod_q F_{\rm ro}(q)", "F_est ~ prod (1-eps_g) prod (1-eps_idle) prod F_ro",
   [(r"\epsilon_g", "Gate error", "", None), (r"\epsilon_{\rm idle}", "Idle thermal-relaxation infidelity", "", None), (r"F_{\rm ro}", "Readout assignment fidelity", "", None)],
   ["independent_errors", "average_gate_fidelities", "no_crosstalk"], "T12", "5", "Model")
eq("classical_cost", r"M = 16\cdot 2^n\ \text{bytes},\qquad t \approx N_{\rm gates}\,2^n\,c_{\rm gate}", "M = 16 2^n bytes; t ~ N_gates 2^n c_gate",
   [("n", "Qubits", "", "run.n_qubits"), (r"c_{\rm gate}", "Per-amplitude gate cost on this host", "ns", "static.host.c_gate")], [], "T12", "4", "Model")
eq("bloch_vector", r"\vec r = \left(\mathrm{Tr}\,\rho X,\ \mathrm{Tr}\,\rho Y,\ \mathrm{Tr}\,\rho Z\right),\qquad |\vec r| \le 1", "r = (Tr rho X, Tr rho Y, Tr rho Z)",
   [(r"\vec r", "Bloch vector", "", "device.qubit[$i].bloch"), (r"\rho", "Reduced density operator", "", None)], [], "T01", "7.1")
eq("partial_trace", r"\rho_A = \mathrm{Tr}_B\,\rho = \sum_j (I\otimes\langle j|)\,\rho\,(I\otimes|j\rangle)", "rho_A = Tr_B rho", [(r"\rho_A", "Reduced state", "", None)], ["little_endian"], "T01", "7.2")
eq("von_neumann_entropy", r"S(\rho) = -\mathrm{Tr}\,\rho\log_2\rho", "S = -Tr rho log2 rho", [("S", "Entropy", "bit", None)], [], "T01", "8")
eq("mutual_information", r"I(A{:}B) = S_A + S_B - S_{AB}", "I(A:B) = S_A + S_B - S_AB", [("S_A", "Entropy of subsystem A", "bit", None)], [], "T01", "8")
eq("concurrence", r"C(\rho) = \max\left(0,\ \lambda_1-\lambda_2-\lambda_3-\lambda_4\right),\qquad \lambda_i = \sqrt{\mathrm{eig}\left(\rho\,(Y\otimes Y)\rho^*(Y\otimes Y)\right)}\downarrow", "C = max(0, l1 - l2 - l3 - l4)",
   [("C", "Concurrence", "", None)], [], "T01", "8")
eq("schmidt", r"|\psi\rangle = \sum_k \sqrt{\lambda_k}\,|u_k\rangle_A|v_k\rangle_B", "psi = sum sqrt(lambda_k) u_k v_k", [(r"\lambda_k", "Schmidt coefficients", "", None)], ["pure_state"], "T01", "7.3")
eq("wigner", r"W(\alpha) = \frac{2}{\pi}\,\mathrm{Tr}\!\left[D(\alpha)\rho D^\dagger(\alpha)\,\Pi\right],\qquad \Pi = (-1)^{a^\dagger a}", "W(alpha) = 2/pi Tr[D(alpha) rho D†(alpha) Pi]",
   [(r"\alpha", "Phase-space point", "", None), ("D", "Displacement operator", "", None), (r"\Pi", "Parity operator", "", None)], [], "T01", "7")
eq("born_rule", r"P(x) = |\langle x|\psi\rangle|^2 = |a_x|^2", "P(x) = |a_x|^2", [("a_x", "Amplitude of basis state x", "", None)], ["little_endian"], "T01", "5.2")
eq("fidelity_pure", r"F(\psi,\phi) = |\langle\psi|\phi\rangle|^2", "F = |<psi|phi>|^2", [("F", "State fidelity", "", None)], ["pure_state"], "T01", "8")
eq("hellinger", r"H^2(p,q) = 1 - \sum_x\sqrt{p(x)q(x)}", "H^2 = 1 - sum sqrt(p q)", [("p, q", "Outcome distributions", "", None)], [], "T01", "8", "Statistical")
eq("vna_s21_notch", r"S_{21}(f) = a e^{i\alpha}e^{-2\pi i f\tau}\left[1 - \frac{Q_l/|Q_c|\,e^{i\phi}}{1 + 2iQ_l\,(f-f_r)/f_r}\right]", "S21 = a e^{i alpha} e^{-2 pi i f tau} [1 - (Ql/|Qc|) e^{i phi}/(1 + 2i Ql (f-fr)/fr)]",
   [("f_r", "Resonance frequency", "GHz", "device.res[$i].f_r"), ("Q_l", "Loaded quality factor", "", None), ("Q_c", "Coupling quality factor", "", None), (r"\phi", "Impedance-mismatch phase", "rad", None)], [], "T05", "6.3", "Model")
eq("q_loaded", r"\frac{1}{Q_l} = \frac{1}{Q_i} + \frac{1}{Q_c},\qquad \kappa = \frac{2\pi f_r}{Q_l}", "1/Ql = 1/Qi + 1/Qc; kappa = 2 pi fr/Ql",
   [("Q_i", "Internal quality factor", "", None), ("Q_c", "Coupling quality factor", "", None), (r"\kappa", "Linewidth", "MHz", "device.res[$i].kappa")], [], "T05", "6.3", "Model")
eq("gaussian_pulse", r"\Omega(t) = A\exp\!\left[-\frac{(t-\tau/2)^2}{2\sigma^2}\right]", "Omega = A exp(-(t - tau/2)^2/(2 sigma^2))", [("A", "Amplitude", "", None), (r"\sigma", "Width", "ns", None)], [], "T07", "4")
eq("phase_noise", r"\mathcal L(f) = 10\log_{10}\frac{S_\phi(f)}{2}\ \ [\mathrm{dBc/Hz}]", "L(f) = 10 log10(S_phi/2)", [(r"\mathcal L", "Single-sideband phase noise", "dBc/Hz", "instr.gen[$i].phase_noise")], [], "T07", "5", "Model")

# ================================================================ gates.json (spec 19 §3 hover docs, 20 §4)
# Matrices are written in the little-endian convention: for `g q[a], q[b]` the basis is |q_b q_a>
# (first listed qubit is the least significant bit).  CX below has control = first argument.
def gate(name, sig, desc, latex, decomposition="", native_for=None, params=None, qubits=1, clifford=False):
    GATES.append({"name": name, "signature": sig, "description": desc, "matrix_latex": latex, "qubits": qubits,
                  "params": params or [], "decomposition": decomposition, "native_for": native_for or [],
                  "clifford": clifford, "theory": anchor("T02", "1.2" if qubits == 1 else "2.2")})

_c = r"\cos\frac{\theta}{2}"; _s = r"\sin\frac{\theta}{2}"
gate("U", "U(θ, φ, λ) q", "OpenQASM 3 primitive single-qubit gate; every single-qubit unitary up to global phase.",
     r"\begin{pmatrix}\cos\frac\theta2 & -e^{i\lambda}\sin\frac\theta2\\ e^{i\varphi}\sin\frac\theta2 & e^{i(\varphi+\lambda)}\cos\frac\theta2\end{pmatrix}",
     r"rz(\varphi+\pi)\,sx\,rz(\theta+\pi)\,sx\,rz(\lambda)", params=["theta", "phi", "lambda"])
gate("gphase", "gphase(γ)", "Global phase e^{iγ}; physically irrelevant alone, matters inside controlled gates.", r"e^{i\gamma}", params=["gamma"])
gate("id", "id q", "Identity (explicit idle of one gate duration).", r"\begin{pmatrix}1&0\\0&1\end{pmatrix}", native_for=["transmon"], clifford=True)
gate("x", "x q", "Pauli X (bit flip, π rotation about x).", r"\begin{pmatrix}0&1\\1&0\end{pmatrix}", "U(\\pi,0,\\pi)", native_for=["transmon"], clifford=True)
gate("y", "y q", "Pauli Y.", r"\begin{pmatrix}0&-i\\i&0\end{pmatrix}", "U(\\pi,\\pi/2,\\pi/2)", clifford=True)
gate("z", "z q", "Pauli Z (phase flip).", r"\begin{pmatrix}1&0\\0&-1\end{pmatrix}", "p(\\pi)", clifford=True)
gate("h", "h q", "Hadamard: maps Z basis to X basis.", r"\frac{1}{\sqrt2}\begin{pmatrix}1&1\\1&-1\end{pmatrix}", "rz(\\pi/2)\\,sx\\,rz(\\pi/2)", clifford=True)
gate("s", "s q", "Phase gate √Z.", r"\begin{pmatrix}1&0\\0&i\end{pmatrix}", "p(\\pi/2)", clifford=True)
gate("sdg", "sdg q", "Inverse phase gate.", r"\begin{pmatrix}1&0\\0&-i\end{pmatrix}", "p(-\\pi/2)", clifford=True)
gate("t", "t q", "T gate (π/8): non-Clifford, counted for fault-tolerant cost.", r"\begin{pmatrix}1&0\\0&e^{i\pi/4}\end{pmatrix}", "p(\\pi/4)")
gate("tdg", "tdg q", "Inverse T gate.", r"\begin{pmatrix}1&0\\0&e^{-i\pi/4}\end{pmatrix}", "p(-\\pi/4)")
gate("sx", "sx q", "√X: the native 90° pulse of transmon devices.", r"\frac12\begin{pmatrix}1+i&1-i\\1-i&1+i\end{pmatrix}", "", native_for=["transmon"], clifford=True)
gate("p", "p(λ) q", "Phase rotation about Z (equal to rz up to global phase).", r"\begin{pmatrix}1&0\\0&e^{i\lambda}\end{pmatrix}", "U(0,0,\\lambda)", params=["lambda"])
gate("rx", "rx(θ) q", "Rotation about x.", r"\begin{pmatrix}" + _c + "&-i" + _s + r"\\-i" + _s + "&" + _c + r"\end{pmatrix}", "rz(-\\pi/2)\\,sx\\,rz(\\theta-\\pi)\\,sx\\,rz(-\\pi/2)... (Euler)", native_for=["ion"], params=["theta"])
gate("ry", "ry(θ) q", "Rotation about y.", r"\begin{pmatrix}" + _c + "&-" + _s + r"\\" + _s + "&" + _c + r"\end{pmatrix}", "rz(0)\\,sx\\,rz(\\theta+\\pi)\\,sx\\,rz(\\pi)... (Euler)", native_for=["ion"], params=["theta"])
gate("rz", "rz(θ) q", "Rotation about z; virtual (frame phase shift) on every shipped device, 0 ns.", r"\begin{pmatrix}e^{-i\theta/2}&0\\0&e^{i\theta/2}\end{pmatrix}", "", native_for=["transmon", "ion"], params=["theta"])
gate("u1", "u1(λ) q", "Legacy phase gate (= p).", r"\begin{pmatrix}1&0\\0&e^{i\lambda}\end{pmatrix}", "p(\\lambda)", params=["lambda"])
gate("u2", "u2(φ, λ) q", "Legacy OpenQASM 2 gate: U(pi/2, phi, lambda) with the stdgates.inc global phase gphase(-(phi+lambda)/2).", r"\frac{1}{\sqrt2}\begin{pmatrix}e^{-i(\varphi+\lambda)/2}&-e^{i(\lambda-\varphi)/2}\\e^{i(\varphi-\lambda)/2}&e^{i(\varphi+\lambda)/2}\end{pmatrix}", r"\mathrm{gphase}(-(\varphi+\lambda)/2)\,U(\pi/2,\varphi,\lambda) = R_z(\varphi)R_y(\pi/2)R_z(\lambda)", params=["phi", "lambda"])
gate("u3", "u3(θ, φ, λ) q", "Legacy OpenQASM 2 gate: U(theta, phi, lambda) with the stdgates.inc global phase gphase(-(phi+lambda)/2).", r"\begin{pmatrix}e^{-i(\varphi+\lambda)/2}\cos\frac{\theta}{2}&-e^{i(\lambda-\varphi)/2}\sin\frac{\theta}{2}\\e^{i(\varphi-\lambda)/2}\sin\frac{\theta}{2}&e^{i(\varphi+\lambda)/2}\cos\frac{\theta}{2}\end{pmatrix}", r"\mathrm{gphase}(-(\varphi+\lambda)/2)\,U(\theta,\varphi,\lambda) = R_z(\varphi)R_y(\theta)R_z(\lambda)", params=["theta", "phi", "lambda"])
gate("cx", "cx c, t", "Controlled-NOT: flips t when c = 1. Basis |t c⟩ (little-endian).", r"\begin{pmatrix}1&0&0&0\\0&0&0&1\\0&0&1&0\\0&1&0&0\end{pmatrix}", "", native_for=["transmon"], qubits=2, clifford=True)
gate("CX", "CX c, t", "Legacy spelling of cx.", r"\text{as } cx", "cx", qubits=2, clifford=True)
gate("cy", "cy c, t", "Controlled-Y.", r"\text{diag-block}(I, Y)", "sdg t; cx c,t; s t", qubits=2, clifford=True)
gate("cz", "cz a, b", "Controlled-Z: symmetric phase −1 on |11⟩.", r"\mathrm{diag}(1,1,1,-1)", "h b; cx a,b; h b", native_for=["transmon_tunable", "transmon_tunable_coupler"], qubits=2, clifford=True)
gate("cp", "cp(λ) a, b", "Controlled phase.", r"\mathrm{diag}(1,1,1,e^{i\lambda})", "p(\\lambda/2) a; cx a,b; p(-\\lambda/2) b; cx a,b; p(\\lambda/2) b", qubits=2, params=["lambda"])
gate("crx", "crx(θ) c, t", "Controlled RX.", r"\text{diag-block}(I, R_x(\theta))", "ABC construction", qubits=2, params=["theta"])
gate("cry", "cry(θ) c, t", "Controlled RY.", r"\text{diag-block}(I, R_y(\theta))", "ry(θ/2) t; cx c,t; ry(-θ/2) t; cx c,t", qubits=2, params=["theta"])
gate("crz", "crz(θ) c, t", "Controlled RZ.", r"\text{diag-block}(I, R_z(\theta))", "rz(θ/2) t; cx c,t; rz(-θ/2) t; cx c,t", qubits=2, params=["theta"])
gate("ch", "ch c, t", "Controlled Hadamard.", r"\text{diag-block}(I, H)", "s t; h t; t t; cx c,t; tdg t; h t; sdg t", qubits=2)
gate("swap", "swap a, b", "Exchange two qubits.", r"\begin{pmatrix}1&0&0&0\\0&0&1&0\\0&1&0&0\\0&0&0&1\end{pmatrix}", "cx a,b; cx b,a; cx a,b", qubits=2, clifford=True)
gate("cu", "cu(θ, φ, λ, γ) c, t", "Controlled U with global phase γ on the controlled branch.", r"\text{diag-block}(I, e^{i\gamma}U(\theta,\varphi,\lambda))", "ABC construction", qubits=2, params=["theta", "phi", "lambda", "gamma"])
gate("ccx", "ccx a, b, t", "Toffoli.", r"\text{8×8 permutation}: |11t\rangle \to |11\bar t\rangle", "6-CNOT, T-count 7 (T02 §3.2)", qubits=3)
gate("cswap", "cswap c, a, b", "Fredkin (controlled swap).", r"\text{8×8 permutation}", "cx b,a; ccx c,a,b; cx b,a", qubits=3)
gate("ecr", "ecr c, t", "Echoed cross-resonance: √2/2 (IX − XY) in the |t c⟩ basis; native CR-device entangler.", r"\frac{1}{\sqrt2}\begin{pmatrix}0&1&0&i\\1&0&-i&0\\0&i&0&1\\-i&0&1&0\end{pmatrix}", "cx = (rz(-π/2) c; sx c...) ecr with single-qubit dressing (T02 §2.3)", native_for=["transmon"], qubits=2, clifford=True)
gate("siswap", "siswap a, b", "√iSWAP: native for tunable-coupler devices.", r"\begin{pmatrix}1&0&0&0\\0&\frac{1}{\sqrt2}&\frac{i}{\sqrt2}&0\\0&\frac{i}{\sqrt2}&\frac{1}{\sqrt2}&0\\0&0&0&1\end{pmatrix}", "two siswap + single-qubit = cz", native_for=["transmon_tunable_coupler"], qubits=2)
gate("iswap", "iswap a, b", "iSWAP.", r"\begin{pmatrix}1&0&0&0\\0&0&i&0\\0&i&0&0\\0&0&0&1\end{pmatrix}", "siswap a,b; siswap a,b", qubits=2, clifford=True)
gate("rxx", "rxx(θ) a, b", "XX rotation exp(−iθ XX/2); rxx(π/2) is the Mølmer–Sørensen gate.", r"\exp(-i\tfrac{\theta}{2}X\otimes X)", "h a; h b; cx a,b; rz(θ) b; cx a,b; h a; h b", native_for=["ion"], qubits=2, params=["theta"])
gate("ms", "ms(θ) a, b", "Mølmer–Sørensen gate (= rxx(θ)) via bichromatic sideband drive.", r"\exp(-i\tfrac{\theta}{2}X\otimes X)", "", native_for=["ion"], qubits=2, params=["theta"])
gate("ryy", "ryy(θ) a, b", "YY rotation.", r"\exp(-i\tfrac{\theta}{2}Y\otimes Y)", "rx(π/2) a,b; rxx? no: sdg,h conjugation of rzz", qubits=2, params=["theta"])
gate("rzz", "rzz(θ) a, b", "ZZ rotation.", r"\exp(-i\tfrac{\theta}{2}Z\otimes Z)", "cx a,b; rz(θ) b; cx a,b", qubits=2, params=["theta"])
gate("measure", "measure q -> c", "Projective Z-basis measurement, result to a classical bit.", r"\{|0\rangle\langle0|,\ |1\rangle\langle1|\}", "", native_for=["transmon", "ion"])
gate("reset", "reset q", "Return qubit to |0⟩ (active or passive per device policy).", r"\rho \to |0\rangle\langle0|", "", native_for=["transmon", "ion"])
gate("barrier", "barrier q...", "Scheduling barrier: no reordering across it.", r"-", "")
gate("delay", "delay[t] q", "Explicit idle of duration t (subject to T1/T2 in the noise model).", r"\exp(-i H_0 t/\hbar)", "")

# ================================================================ component descriptors
def comp(id, name, category, function, summary, equations, spec_sheet, theory, tooltip, geometry,
         parent=None, lod=None, instrument=None, model_name=None, tooltips=None, bindings=None):
    d = {"id": id, "name": name, "category": category, "parent": parent, "function": function,
         "physics": {"summary": summary, "equations": equations}, "spec_sheet": spec_sheet, "theory": theory,
         "tooltip": tooltip, "geometry": geometry,
         "lod": lod or [{"max_m": 2.0, "detail": "full"}, {"max_m": 8.0, "detail": "simple"}, {"max_m": 1e9, "detail": "hidden"}]}
    if bindings: d["live_bindings"] = bindings
    if model_name: d["model_name"] = model_name
    if instrument: d["instrument"] = instrument
    if tooltips: d["tooltips"] = tooltips
    COMPONENTS.append(d)

def row(field, unit, typical=None, binding=None, cls=None):
    r = {"field": field, "unit": unit}
    if typical is not None: r["typical"] = typical
    if binding: r["binding"] = binding; r["class"] = cls or "Model"
    return r

# ---------------------------------------------------------------- 3.1 dilution refrigerator
T08 = lambda s: anchor("T08", s); T07 = lambda s: anchor("T07", s); T05 = lambda s: anchor("T05", s); T06 = lambda s: anchor("T06", s)
STAGE_T = {"rt": 293, "s50": 45, "s4": 3.5, "still": 0.85, "cp": 0.10, "mxc": 0.015}

# Spec 17 §10 per-group ranges: the fridge exterior stays at full detail to 6 m (the Overview
# bookmark is 8 m away), the interior plates and shields to 4 m (the Fridge bookmark is 2.3 m).
LOD_EXTERIOR = [{"max_m": 6.0, "detail": "full"}, {"max_m": 30.0, "detail": "simple"}, {"max_m": 1e9, "detail": "hidden"}]
LOD_INTERIOR = [{"max_m": 4.0, "detail": "full"}, {"max_m": 12.0, "detail": "simple"}, {"max_m": 1e9, "detail": "hidden"}]
comp("fridge_frame", "Refrigerator support frame", "structure",
     "Aluminium T-slot extrusion frame (40 × 40 profile, corner brackets, levelling feet) that carries the whole cryostat: the 300 K top plate rests on rubber isolation blocks on its upper cross beams and the vacuum can is lowered from it on the gantry. Its stiffness sets the lowest mechanical resonance of the insert, which matters because the pulse-tube head vibrates at 1.4 Hz and its harmonics couple into wiring and, through flux, into tunable qubits.",
     "Mechanical support; sets vibration transfer from the pulse tube to the insert.", [],
     [row("width", "m", [1.2, 1.6]), row("depth", "m", [1.2, 1.6]), row("height", "m", [2.4, 2.8]), row("first mechanical resonance", "Hz", [8, 20])],
     [], "Support frame for the cryostat insert and vacuum can.", {"generator": "Frame", "width_m": 1.4, "depth_m": 1.4, "height_m": 2.6, "profile_mm": 40}, parent="room", lod=LOD_EXTERIOR)
comp("gantry_hoist", "Gantry hoist", "structure",
     "Motorised trolley on the frame's top beams that lowers and raises the outer vacuum can and the radiation shields. Opening the fridge is a multi-hour operation: the can must be lowered fully before any stage is accessible, and every shield is removed in order from the outside in. The simulator drives this with the cryo.ovc.lowered value.",
     "Mechanical; no thermal role.", [], [row("span", "m", [1.2, 1.6]), row("travel", "m", [1.4, 1.8]), row("lowered fraction", "", binding="cryo.ovc.lowered", cls="Illustrative")],
     [], "Hoist that lowers the vacuum can and shields.", {"generator": "Frame", "span_m": 1.4, "travel_m": 1.6}, parent="fridge_frame", lod=LOD_EXTERIOR)
comp("top_plate_300K", "300 K top plate", "stage",
     "Room-temperature flange of the insert. Every wire, coaxial line, pumping line and the pulse-tube head pass through it, so its feedthrough ring is the vacuum boundary of the cryostat. It is also the warm end of every conduction heat path into the fridge: the conductive load on the 50 K stage is set by the temperature difference from this plate.",
     "Warm boundary of all conduction paths; vacuum seal.", ["conduction_load"],
     [row("radius", "m", [0.28, 0.32]), row("thickness", "m", [0.015, 0.025]), row("feedthroughs", "", [48, 200]), row("temperature", "K", binding="cryo.stage.rt.T", cls="Model")],
     [T08("3")], "Room-temperature top flange with all feedthroughs.", {"generator": "Plate", "r_m": 0.30, "t_m": 0.02, "holes": "feedthrough_ring", "kf_ports": 2, "viewport": True}, parent="fridge_frame", lod=LOD_EXTERIOR)
comp("feedthrough_sma", "SMA vacuum feedthrough", "wiring",
     "Hermetic SMA bulkhead in the top plate carrying one microwave line from the room-temperature rack into the vacuum. Its insertion loss is negligible compared with the cryogenic attenuators, but a leaking feedthrough is the most common cause of a slowly rising OVC pressure. The input power referenced here is the power leaving the rack cable.",
     "Vacuum-tight RF connector; entry point of each line.", ["dbm_to_watts"],
     [row("frequency range", "GHz", [0, 18]), row("insertion loss", "dB", [0.1, 0.3]), row("input power", "dBm", binding="wiring.line[$line].rt.P_in", cls="Model")],
     [T07("8")], "Hermetic SMA feedthrough for one microwave line.", {"generator": "SmaConnector"}, parent="top_plate_300K")
comp("ovc", "Outer vacuum can (OVC)", "vacuum",
     "Brushed stainless-steel can sealed to the top plate by an O-ring under a 48-bolt flange; it holds the insulating vacuum (below 1e-5 mbar cold). Without it, gas conduction and convection would dump kilowatts into the 50 K stage. The dished (2:1 ellipsoidal) base resists the 1 bar external load. Its pressure is the first quantity checked before starting a cooldown and the first to move when a leak or a warm-up begins.",
     "Insulating vacuum; removes gas conduction as a heat path.", ["turbo_pressure"],
     [row("radius", "m", [0.26, 0.30]), row("height", "m", [1.3, 1.4]), row("wall", "mm", [5, 8]), row("pressure", "mbar", binding="cryo.ovc.pressure", cls="Model"), row("lowered fraction", "", binding="cryo.ovc.lowered", cls="Illustrative")],
     [T08("5")], "Outer vacuum can: the insulating vacuum boundary.", {"generator": "Can", "r_m": 0.28, "h_m": 1.35, "t_m": 0.006, "base": "dished", "open": "top", "flange": True, "flange_t_m": 0.012, "flange_w_m": 0.015, "flange_bolts": 48}, parent="top_plate_300K", lod=LOD_EXTERIOR)

STAGE_T_MM = {"rt": 20, "s50": 10, "s4": 12, "still": 10, "cp": 10, "mxc": 12}  # plate thickness (spec 17 §3.1 amended)
def stage(id, name, key, r, cool_note, theory_sec, extra_rows=(), fn=None):
    comp(id, name, "stage",
         fn or f"Gold-plated copper plate that defines the {name.split(' (')[0].lower()} isotherm of the insert. Every wire passing through is thermally anchored here so that the heat it conducts from the warmer stage is intercepted at this temperature rather than reaching the colder stages. {cool_note}",
         "Isothermal node of the thermal network; intercepts conduction and radiation loads.", ["conduction_load", "cooldown_lumped"],
         [row("radius", "m", [r - 0.02, r + 0.02]), row("nominal temperature", "K", [STAGE_T[key] * 0.8, STAGE_T[key] * 1.2]),
          row("thickness", "mm", [STAGE_T_MM[key] * 0.8, STAGE_T_MM[key] * 1.2]),
          row("temperature", "K", binding=f"cryo.stage.{key}.T", cls="Numerical"), row("heat load", "W", binding=f"cryo.stage.{key}.P_load", cls="Model"),
          row("cooling power", "W", binding=f"cryo.stage.{key}.P_cool", cls="Model"), *extra_rows],
         [T08(theory_sec)], f"{name}: isothermal stage plate.", {"generator": "Plate", "r_m": r, "t_m": STAGE_T_MM[key] * 1e-3, "holes": "bolt_circle"}, parent="fridge_interior", lod=LOD_INTERIOR)

stage("stage_50K", "50 K stage (PT1)", "s50", 0.25, "It is cooled by the first stage of the pulse tube (about 40 W at 45 K) and carries the outer radiation shield, so it absorbs almost all of the 300 K radiation.", "3")
stage("stage_4K", "4 K stage (PT2)", "s4", 0.23, "It is cooled by the second pulse-tube stage (1.5 W at 4.2 K), hosts the HEMT amplifiers and the first 20 dB of drive attenuation, and condenses the incoming helium mixture.", "3",
      [row("HEMT dissipation", "W", binding="wiring.hemt.P_total", cls="Model")])
stage("stage_still", "Still stage", "still", 0.21, "The still evaporates 3He from the dilute phase at 0.7–0.9 K; the still heater sets the circulation rate and therefore the mixing-chamber cooling power.", "1.3",
      [row("still heater power", "mW", binding="cryo.stage.still.P_heater", cls="Model"), row("3He flow", "mmol/s", binding="cryo.flow.n3", cls="Model")])
stage("stage_cp", "Cold plate", "cp", 0.19, "An intermediate anchor at about 100 mK between the still and the mixing chamber, cooled by the returning dilute stream; it is where the readout-line attenuators and the drive_60dB extra attenuator sit.", "1.3")
stage("stage_mxc", "Mixing chamber stage", "mxc", 0.19, "Base-temperature plate (10–20 mK) bolted to the mixing-chamber vessel where the 3He crosses the phase boundary and absorbs heat. The sample, isolators, JPA and final attenuators live here; its temperature sets the residual thermal population of the qubits.", "1.2",
      [row("MXC heater power", "µW", binding="cryo.stage.mxc.P_heater", cls="Model"), row("cooling margin", "µW", binding="cryo.stage.mxc.margin", cls="Model")],
      fn="Base-temperature copper plate bolted to the mixing-chamber vessel, where circulating 3He crosses the phase boundary from the concentrated to the dilute phase and absorbs the enthalpy of mixing. Its cooling power falls as T², so every microwatt of wiring or attenuator load raises the base temperature, and that temperature sets the residual excited-state population of every qubit on the chip.")

def shield(id, name, key, r, h, sec="4"):
    comp(id, name, "shield",
         f"Thin-walled can bolted to the {key} stage that intercepts thermal radiation from the next warmer shield. Radiation scales as T⁴, so the outermost shield does the heavy lifting while the inner ones mainly block stray infrared that would otherwise create quasiparticles in the superconducting films. Shields are removed from the outside in when the fridge is opened.",
         "Radiation shield; reduces the T⁴ radiative load on the stage it is anchored to.", ["radiation_load"],
         [row("radius", "m", [r - 0.01, r + 0.01]), row("height", "m", [h - 0.05, h + 0.05]), row("emissivity", "", [0.02, 0.1]), row("temperature", "K", binding=f"cryo.stage.{key}.T", cls="Numerical")],
         [T08(sec)], f"{name}: radiation shield.", {"generator": "Can", "r_m": r, "h_m": h, "t_m": 0.002, "base": "dished", "open": "top", "flange": True, "flange_t_m": 0.008, "flange_w_m": 0.012, "flange_bolts": int(round(2 * 3.14159 * r / 0.06))}, parent=f"stage_{ {'s50':'50K','s4':'4K','still':'still'}[key] }", lod=LOD_INTERIOR)
shield("shield_50K", "50 K radiation shield", "s50", 0.245, 1.15)
shield("shield_4K", "4 K radiation shield", "s4", 0.225, 0.95)
shield("shield_still", "Still radiation shield", "still", 0.205, 0.75)

comp("pulse_tube_head", "Two-stage pulse-tube cryocooler", "cryocooler",
     "Cryogen-free pre-cooler mounted on the top plate. Compressed helium gas is cycled at 1.4 Hz through a regenerator and two pulse tubes; expansion cools the first stage to about 45 K (40 W capacity) and the second to about 3.5 K (1.5 W at 4.2 K). It provides all the cooling above 1 K and condenses the mixture; the dilution unit only works below the 4 K stage. It is also the dominant vibration source.",
     "Provides the 45 K and 4 K cooling powers of the capacity map.", ["pt_capacity", "cooldown_lumped"],
     [row("first-stage capacity", "W", [35, 45]), row("second-stage capacity at 4.2 K", "W", [1.0, 2.0]), row("operating frequency", "Hz", [1.2, 1.5]),
      row("stage-1 temperature", "K", binding="cryo.pt.stage1.T", cls="Numerical"), row("stage-2 temperature", "K", binding="cryo.pt.stage2.T", cls="Numerical"), row("running", "", binding="cryo.pt.running", cls="Model")],
     [T08("2")], "Pulse-tube cryocooler: 45 K and 4 K pre-cooling.", {"generator": "Cylinder", "r_m": 0.06, "h_m": 0.45, "caps": True, "attachments": ["rotary_valve_box"]}, parent="top_plate_300K")
comp("pt_flex_lines", "Pulse-tube helium flex lines", "cryocooler",
     "Stainless flexible lines carrying high- and low-pressure helium between the remote compressor and the pulse-tube head. Their length and routing are chosen to decouple compressor vibration; the pressures they carry are monitored because a loss of pressure difference is the first sign of compressor failure and leads to a warm-up of the 4 K stage within hours.",
     "Helium supply to the pulse tube; no thermal role in the insert.", [],
     [row("length", "m", [10, 20]), row("high pressure", "bar", binding="cryo.pt.p_high", cls="Model"), row("low pressure", "bar", binding="cryo.pt.p_low", cls="Model")],
     [T08("2")], "Helium lines from the compressor to the pulse-tube head.", {"generator": "Tube", "r_m": 0.012, "path": "spline_to_wall"}, parent="pulse_tube_head")
comp("pt_remote_motor", "Pulse-tube rotary-valve motor", "cryocooler",
     "The valve motor that switches the pulse tube between high and low pressure is mounted on the frame, not the cold head, so that its 1.4 Hz mechanical cycle reaches the insert only through a flexible line. This reduces vibration-induced flux noise and microphonic modulation of readout lines by more than an order of magnitude compared with a head-mounted valve.",
     "Vibration isolation of the valve drive.", [], [row("cycle frequency", "Hz", binding="cryo.pt.freq", cls="Model")],
     [T08("8")], "Remote rotary valve for the pulse tube.", {"generator": "Box", "w_m": 0.2, "h_m": 0.15, "d_m": 0.2}, parent="fridge_frame")
comp("condensing_line", "Condensing line", "circulation",
     "Capillary that returns the 3He-rich mixture from the room-temperature gas handling system into the fridge. It is thermally anchored at 4 K where the gas liquefies, then passes a flow impedance so that Joule–Thomson expansion cools it further before it enters the still heat exchanger. The circulation rate through it is what sets the cooling power at the mixing chamber.",
     "Return path of the circulating 3He; condensation at 4 K.", ["still_flow", "cooling_power_mxc"],
     [row("inner diameter", "mm", [0.5, 1.5]), row("circulation rate", "mmol/s", binding="cryo.flow.n3", cls="Model"), row("inlet temperature", "K", binding="cryo.line.condense.T_in", cls="Numerical")],
     [T08("1.3")], "Condensing line: 3He return into the fridge.", {"generator": "Tube", "r_m": 0.0005, "path": "s4_to_still"}, parent="stage_4K")
comp("flow_impedance", "Flow impedance", "circulation",
     "A short capillary of very high flow impedance (about 1e12 m⁻³) placed after the 4 K condenser. It maintains the pressure needed to keep the mixture liquid upstream while letting it expand and cool on the downstream side. Too low an impedance and the mixture does not condense; too high and the circulation rate, and with it the cooling power, is throttled.",
     "Pressure drop that enables condensation and Joule–Thomson cooling.", ["still_flow"],
     [row("impedance", "1/m³", [5e11, 2e12]), row("condensing pressure", "bar", binding="cryo.flow.p_condense", cls="Model")],
     [T08("1.3")], "Flow impedance after the condenser.", {"generator": "Spiral", "r_m": 0.01, "pitch_m": 0.002, "turns": 8, "tube_r_m": 0.0003}, parent="condensing_line")
comp("hx_continuous", "Continuous heat exchanger", "circulation",
     "Counter-flow tube-in-tube spiral between the still and the cold plate in which the incoming concentrated 3He is cooled by the outgoing cold dilute stream. Its effectiveness determines the temperature at which 3He arrives at the step exchangers, and therefore the T_ex term that subtracts from the mixing-chamber cooling power.",
     "Pre-cools the incoming 3He against the returning dilute stream.", ["cooling_power_general"],
     [row("length", "m", [1.0, 1.5]), row("inlet temperature", "K", binding="cryo.hx.cont.T_in", cls="Numerical"), row("outlet temperature", "K", binding="cryo.hx.cont.T_out", cls="Numerical")],
     [T08("1.3")], "Counter-flow heat exchanger between still and cold plate.", {"generator": "Spiral", "r_m": 0.04, "pitch_m": 0.01, "turns": 12, "tube_r_m": 0.003, "core_r_m": 0.008}, parent="stage_still")
comp("hx_step", "Step (sintered silver) heat exchanger", "circulation",
     "Below about 50 mK the Kapitza boundary resistance between helium and metal grows as T⁻³, so the continuous exchanger becomes ineffective. Stacked blocks packed with sintered silver powder provide square metres of surface area in a few cubic centimetres; four of them in series bring the incoming 3He close to the mixing-chamber temperature.",
     "Large-area exchangers that overcome Kapitza resistance below 50 mK.", ["kapitza", "cooling_power_general"],
     [row("count", "", [3, 6]), row("surface area per block", "m²", [1, 5]), row("block temperature", "K", binding="cryo.hx.step[$i].T", cls="Numerical")],
     [T08("1.3")], "Sintered-silver step heat exchanger.", {"generator": "Box", "w_m": 0.04, "h_m": 0.03, "d_m": 0.04, "count": 4}, parent="stage_cp")
comp("still_heater", "Still heater", "heater",
     "Resistor bonded to the still. Its power sets the 3He evaporation rate and hence the circulation rate through the whole dilution loop: more still power means more cooling power at the mixing chamber, up to the point where the still becomes too warm (above 0.9 K) and starts evaporating 4He, which contaminates the circulating gas and degrades performance.",
     "Controls the circulation rate via still evaporation.", ["still_flow"],
     [row("resistance", "Ω", [50, 200]), row("power", "mW", binding="cryo.stage.still.P_heater", cls="Model")],
     [T08("1.3")], "Still heater: sets the 3He circulation rate.", {"generator": "Box", "w_m": 0.01, "h_m": 0.005, "d_m": 0.01}, parent="stage_still")
comp("mxc_heater", "Mixing-chamber heater", "heater",
     "Resistor on the mixing-chamber plate used to regulate the base temperature above its natural minimum, to measure the cooling-power curve (applied power versus resulting temperature), and to warm the sample stage deliberately, for example to measure the temperature dependence of qubit coherence or resonator loss.",
     "Applies a known heat load for temperature regulation and cooling-power measurements.", ["cooling_power_mxc"],
     [row("resistance", "Ω", [100, 150]), row("power", "µW", binding="cryo.stage.mxc.P_heater", cls="Model")],
     [T08("6")], "Mixing-chamber heater for regulation and cooling-power curves.", {"generator": "Box", "w_m": 0.01, "h_m": 0.005, "d_m": 0.01}, parent="stage_mxc")
comp("thermometer_ruo2", "Ruthenium-oxide thermometer", "sensor",
     "Thick-film RuO₂ resistor whose resistance rises steeply below 1 K following variable-range hopping. Read with a four-wire AC resistance bridge at nanowatt excitation: at 10 mK a few picowatts of self-heating already shift the reading. Calibrated against a fixed-point device or noise thermometer; it is the primary sensor at the mixing chamber and cold plate.",
     "Resistance thermometry below 1 K; self-heating limited at base.", ["ruo2_thermometer"],
     [row("range", "K", [0.01, 40]), row("excitation", "nW", [0.001, 1]), row("resistance", "kΩ", binding="cryo.thermo[$i].R", cls="Model"), row("temperature", "K", binding="cryo.stage.$stage.T", cls="Numerical")],
     [T08("8")], "RuO₂ thermometer for the sub-kelvin stages.", {"generator": "Box", "w_m": 0.006, "h_m": 0.003, "d_m": 0.01}, parent="stage_mxc",
     instrument={"class": "thermometer_ruo2", "settings_schema": "instr/thermometer.schema.json", "channels": ["T", "R"]}, model_name="RuO2 resistance thermometer with AC bridge")
comp("thermometer_cernox", "Cernox thermometer", "sensor",
     "Zirconium oxynitride thin-film resistor with low magnetic-field sensitivity, used from room temperature down to about 1.4 K. It tracks the 50 K and 4 K stages during cooldown and reports the pulse-tube stage temperatures; below 1 K its sensitivity is too low and the RuO₂ sensors take over.",
     "Resistance thermometry 1.4–325 K.", [],
     [row("range", "K", [1.4, 325]), row("resistance", "kΩ", binding="cryo.thermo[$i].R", cls="Model"), row("temperature", "K", binding="cryo.stage.$stage.T", cls="Numerical")],
     [T08("8")], "Cernox thermometer for the 4 K and 50 K stages.", {"generator": "Box", "w_m": 0.006, "h_m": 0.003, "d_m": 0.01}, parent="stage_4K",
     instrument={"class": "thermometer_cernox", "settings_schema": "instr/thermometer.schema.json", "channels": ["T", "R"]}, model_name="Cernox thin-film resistance thermometer")
comp("mag_shield_mumetal", "μ-metal magnetic shield", "shield",
     "High-permeability nickel–iron can around the sample stage that diverts the Earth's field and stray fields from pumps and motors. Residual fields below about 1 μT at the chip are required: a SQUID loop of 100 μm² sees 0.05 Φ₀ per μT, and static flux offsets shift tunable qubits off their sweet spots while low-frequency fluctuations add dephasing.",
     "Static field attenuation to the μT level.", ["flux_tunability"],
     [row("shielding factor", "", [100, 1000]), row("residual field target", "µT", [0, 1]), row("radius", "m", [0.05, 0.08])],
     [T05("4"), T08("8")], "μ-metal can: attenuates static magnetic fields.", {"generator": "Can", "r_m": 0.06, "h_m": 0.20, "t_m": 0.001, "base": "flat", "open": "top"}, parent="stage_mxc")
comp("mag_shield_al", "Superconducting aluminium shield", "shield",
     "Aluminium can inside the μ-metal shield. Below its critical temperature of 1.2 K it becomes superconducting and expels or freezes in the field present at the transition, so the field at the chip becomes constant in time even if the external field drifts. It must be cooled in the lowest possible field, which is why it sits inside the μ-metal can.",
     "Flux freezing below T_c = 1.2 K stabilises the field at the chip.", [],
     [row("critical temperature", "K", [1.1, 1.3]), row("superconducting", "", binding="cryo.stage.mxc.T", cls="Model")],
     [T05("4")], "Superconducting Al can: freezes the field at the chip.", {"generator": "Can", "r_m": 0.055, "h_m": 0.19, "t_m": 0.002, "base": "flat", "open": "top"}, parent="mag_shield_mumetal")
comp("cold_finger", "Cold finger", "structure",
     "Gold-plated copper post extending from the mixing-chamber plate down into the magnetic shields, carrying the sample puck. Gold plating avoids the oxide layer that would add thermal contact resistance; the bolted joint is torqued to specification because at 15 mK the thermal conductance of a joint is dominated by its contact pressure.",
     "Thermal link from the mixing chamber to the sample.", ["conduction_load"],
     [row("length", "m", [0.1, 0.2]), row("temperature", "K", binding="cryo.stage.mxc.T", cls="Numerical")],
     [T08("3")], "Copper cold finger carrying the sample puck.", {"generator": "Cylinder", "r_m": 0.02, "h_m": 0.15, "caps": True}, parent="stage_mxc")
comp("sample_puck", "Sample puck", "sample",
     "Removable copper cylinder with SMA bulkheads that holds the chip package and mates to the cold finger. Pucks let a chip be exchanged without rewiring the fridge: the microwave lines terminate in a bulkhead plate and the puck plugs into it. Its temperature is the best available proxy for the chip temperature.",
     "Exchangeable sample carrier; thermal and RF interface to the fridge.", [],
     [row("diameter", "m", [0.04, 0.06]), row("ports", "", [8, 24]), row("temperature", "K", binding="device.T_sample", cls="Numerical")],
     [T08("9")], "Sample puck: exchangeable chip carrier.", {"generator": "Cylinder", "r_m": 0.025, "h_m": 0.05, "caps": True, "attachments": ["sma_bulkheads"]}, parent="cold_finger")
comp("chip_package", "Chip package", "sample",
     "Copper enclosure with a printed-circuit board and a lid that forms a cavity around the chip. The cavity's lowest mode must lie well above the qubit and readout band, otherwise it mediates crosstalk and Purcell loss; the lid height and any interior posts set that frequency. The package also provides the ground reference for all on-chip coplanar waveguides.",
     "Encloses the chip; its cavity mode must sit above the operating band.", ["purcell_decay"],
     [row("cavity mode", "GHz", binding="device.package.f_cavity", cls="Model"), row("temperature", "K", binding="device.T_sample", cls="Numerical"), row("interior size", "mm", [10, 20])],
     [T05("6.4")], "Chip package with cavity and PCB.", {"generator": "Box", "w_m": 0.03, "h_m": 0.01, "d_m": 0.03, "chamfer": 0.001}, parent="sample_puck")
comp("pcb", "Interposer PCB", "sample",
     "Multilayer board with 50 Ω coplanar launchers that route signals from the package SMA connectors to the wirebond pads at the chip edge. Launcher impedance and via stitching determine reflections seen by the readout line; the transition from PCB to chip is the largest impedance discontinuity in the whole chain.",
     "50 Ω routing from connectors to the chip edge.", [],
     [row("ports", "", [8, 40]), row("trace impedance", "Ω", [48, 52])],
     [T05("6.3")], "PCB routing SMA launchers to wirebond pads.", {"generator": "Plate", "r_m": 0.014, "t_m": 0.0016, "shape": "square"}, parent="chip_package")

# ---------------------------------------------------------------- 3.2 wiring
comp("coax_segment", "Cryogenic coaxial segment", "wiring",
     "One stage-to-stage run of semi-rigid coax. Material is the design choice: stainless steel (0.86 mm) between 300 K and 4 K because its low thermal conductivity limits the conducted load; cupronickel below 4 K; and superconducting NbTi on the output line between the mixing chamber and the HEMT, where the signal is a few photons and any resistive loss costs readout fidelity. Its attenuation is lumped at the cold end for the noise budget.",
     "Conduction heat path between stages; RF loss on the signal.", ["conduction_load", "attn_noise_cascade"],
     [row("outer diameter", "mm", [0.86, 2.19]), row("length", "m", [0.1, 0.3], binding="static.length_m", cls="Model"), row("attenuation at 5 GHz", "dB/m", [0.5, 10]),
      row("conducted heat", "W", binding="wiring.line[$line].seg[$j].P_cond", cls="Model"), row("warm-end temperature", "K", binding="wiring.line[$line].seg[$j].T_hot", cls="Numerical"), row("cold-end temperature", "K", binding="wiring.line[$line].seg[$j].T_cold", cls="Numerical")],
     [T08("4"), T07("9")], "Coax segment between two stages; conducts heat, attenuates signal.", {"generator": "Tube", "r_m": 0.00043, "path": "catmull_rom_between_stages"}, parent="wiring")
comp("attenuator", "Cryogenic attenuator", "wiring",
     "Reduces the drive-line signal and, more importantly, the thermal noise arriving from warmer stages. An attenuator at temperature T_s replaces the incoming noise with its own thermal noise at T_s: a 20 dB attenuator passes 1 % of the incoming noise photons and adds 99 % of a T_s thermal population. The stage it sits on therefore matters as much as its value, and the dissipated drive power is a heat load on that stage.",
     "Cascaded attenuators set the effective noise temperature at the qubit drive port.", ["attn_noise_cascade", "thermal_photon_number", "attn_dissipation"],
     [row("attenuation", "dB", [0, 30], binding="static.A_dB", cls="Model"), row("frequency range", "GHz", [0, 18]), row("power rating", "W", [0.5, 2]),
      row("dissipated power", "W", binding="wiring.line[$line].attn[$k].P_diss", cls="Model"), row("output thermal photons", "", binding="wiring.line[$line].attn[$k].n_th", cls="Model")],
     [T07("9"), T08("4")], "Attenuator: thermalises drive-line noise to this stage.", {"generator": "CylinderSma", "length_mm": 25, "diameter_mm": 9}, parent="wiring")
comp("ir_filter", "Infrared (Eccosorb) filter", "wiring",
     "Short coax section filled with a lossy magnetically loaded epoxy that absorbs radiation above roughly 10 GHz while passing the 4–8 GHz band with 1–3 dB loss. Stray infrared photons propagating down the line break Cooper pairs in the aluminium films; the resulting quasiparticles limit T1 and shift resonator frequencies. Placed at the mixing chamber as the last element before the chip.",
     "Blocks pair-breaking infrared photons from reaching the chip.", ["attn_dissipation"],
     [row("cutoff", "GHz", [8, 15]), row("insertion loss in band", "dB", [1, 3]), row("dissipated power", "W", binding="wiring.line[$line].irf.P_diss", cls="Model")],
     [T07("9"), T05("10")], "Eccosorb filter against infrared photons.", {"generator": "CylinderSma", "length_mm": 40, "diameter_mm": 9}, parent="stage_mxc")
comp("lpf", "Low-pass filter", "wiring",
     "Reflective LC or RC low-pass filter that bounds the bandwidth of a line. On flux lines the cutoff (tens of MHz to 1 GHz) sets how fast a flux pulse can be and blocks high-frequency noise that would drive the qubit; on RF lines a 12 GHz cutoff blocks harmonics of the mixer and generator that would otherwise land on the |1⟩→|2⟩ or resonator transitions.",
     "Limits line bandwidth; defines the flux-pulse bandwidth constraint.", ["lpf_cutoff"],
     [row("cutoff", "GHz", [0.001, 12], binding="static.f_c", cls="Model"), row("stopband rejection", "dB", [40, 80])],
     [T07("11")], "Low-pass filter bounding line bandwidth.", {"generator": "Box", "w_m": 0.02, "h_m": 0.01, "d_m": 0.03}, parent="wiring")
# ---------------------------------------------------------------- 3.1 chandelier structure and circulation bodies (fridge detail pass)
comp("support_post", "Stage support post", "structure",
     "One of the six posts that carry each stage plate from the plate above it, standing on the bolt circle with a hex collar at both ends. Its material is chosen from the thermal conductivity integral Θ(T_c, T_h) of T08 §4: between 300 K and 4 K a thin-walled stainless tube (Ø 20 mm, 0.5 mm wall, A ≈ 31 mm²) conducts about 30 mW per post from 300 K to 50 K and 1.3 mW from 50 K to 4 K, which the pulse tube absorbs easily; below 4 K the same stainless would leak 5 µW per post into the still and 0.2 µW into the mixing chamber, so the posts there are G10 or Vespel rod (Ø 12 mm), whose Θ is 3–10 times smaller and whose conduction falls to tens of nanowatts, below the cooling margin of the mixing chamber. Length, section and material fix the load; the plates stay parallel because six posts over-constrain the ring.",
     "Conduction path between stages; material chosen by the conductivity integral.", ["conduction_load"],
     [row("length", "m", [0.10, 0.25]), row("diameter", "mm", [12, 20]), row("wall", "mm", [0.5, 6]), row("material", "", None), row("conducted heat", "W", [1e-8, 3e-2]),
      row("upper stage temperature", "K", binding="cryo.stage.$stage.T", cls="Numerical"), row("stage heat load", "W", binding="cryo.stage.$stage.P_load", cls="Model")],
     [T08("4"), T08("3")], "Stainless (above 4 K) or G10 (below) post carrying the stage plate.",
     {"generator": "Post", "d_m": 0.020, "L_m": 0.22, "wall_m": 0.0005, "collar_m": 0.008}, parent="fridge_interior")
comp("pt_regenerator", "Pulse-tube regenerator stage", "cryocooler",
     "A pulse-tube cryocooler is a closed-cycle helium refrigerator with no moving parts at the cold end: a compressor pressurises and depressurises helium at 1.4 Hz through a regenerator packed with lead and rare-earth spheres, and the gas expanding in the pulse tube absorbs heat at the cold end. This stainless tube is one of its two stages, mounted through the plates above with its cold flange bolted to the stage plate: the first stage (Ø 40 mm) reaches about 45 K and lifts 40 W, the second (Ø 30 mm) about 4.2 K and 1.5 W (0.9 W at 3.5 K, no-load 2.8 K). Because the rotary valve and compressor sit remotely, the only vibration reaching the insert is the 1.4 Hz pressure cycle in the tube itself, which the copper braids to the plate decouple.",
     "Two-stage pulse-tube stage: 40 W at 45 K, 1.5 W at 4.2 K.", ["cooldown_lumped"],
     [row("stage diameter", "mm", [30, 40]), row("cooling power", "W", [1.5, 40]), row("cycle frequency", "Hz", [1.2, 1.5]),
      row("stage temperature", "K", binding="cryo.stage.$stage.T", cls="Numerical"), row("running", "", binding="cryo.pt.running", cls="Model")],
     [T08("2"), T08("8")], "Pulse-tube stage tube ending on the 50 K or 4 K plate.",
     {"generator": "Cylinder", "r_m": 0.02, "h_m": 0.25, "caps": True, "attachments": ["flange"], "flange_at": "bottom"}, parent="pulse_tube_head")
comp("thermal_braid", "Copper thermal braid", "cryocooler",
     "Bundle of oxygen-free copper braids linking the pulse-tube stage flange to the stage plate. Copper with a residual resistivity ratio of 100 conducts about 1000 times better than stainless at 4 K (Θ(4 K, 50 K) ≈ 4.6·10⁴ W/m against 1.4·10² W/m), so a few braids of 20 mm² total section pass the full 1.5 W with a temperature drop of a few tens of millikelvin; being flexible they also stop the 1.4 Hz pulse-tube motion (micrometre amplitude) from shaking the plate and, through the wiring, the mixing chamber. The trade is stiffness against conductance: more strands mean a smaller ΔT but a stiffer link.",
     "High-conductance flexible link; vibration decoupling of the pulse tube.", ["conduction_load"],
     [row("section", "mm²", [10, 40]), row("length", "mm", [40, 100]), row("temperature drop", "mK", [10, 100]),
      row("stage temperature", "K", binding="cryo.stage.$stage.T", cls="Numerical")],
     [T08("2"), T08("4")], "OFHC copper braid between the pulse-tube stage and its plate.",
     {"generator": "Tube", "r_m": 0.0025, "bundle": 5, "bundle_pitch_m": 0.0055, "path": "straight_0.06m"}, parent="pt_regenerator")
comp("still", "Still", "circulation",
     "Evaporator on the still plate held at 0.7–0.9 K. The dilute phase enters from the mixing chamber and, because the vapour pressure of 3He is about a thousand times that of 4He at this temperature, the vapour pumped off the still is over 95 % 3He even though the liquid is only a few percent 3He. That selective evaporation is what drives the circulation: the osmotic pressure difference pulls 3He across the phase boundary in the mixing chamber to replace what leaves. The still heater sets the evaporation rate (a few hundred µW to a few mW), and with it the circulation rate ṅ₃ and, through the cooling power 84 ṅ₃ T², the base temperature. A film burner on its neck stops the superfluid 4He film creeping up the pumping line.",
     "Selective evaporation of 3He drives the circulation.", ["still_flow"],
     [row("temperature", "K", [0.7, 0.9]), row("3He fraction in vapour", "", [0.9, 0.99]), row("volume", "cm³", [50, 150]),
      row("temperature (live)", "K", binding="cryo.stage.still.T", cls="Numerical"), row("3He flow", "mmol/s", binding="cryo.flow.n3", cls="Model"), row("heater power", "mW", binding="cryo.stage.still.P_heater", cls="Model")],
     [T08("1.3"), T08("1.1")], "Still: evaporates 3He from the dilute phase at about 0.8 K.",
     {"generator": "Cylinder", "r_m": 0.03, "h_m": 0.04, "caps": True, "attachments": ["flange"], "flange_at": "bottom"}, parent="stage_still")
comp("still_pumping_line", "Still pumping line", "circulation",
     "Large-bore stainless tube (Ø 50 mm) that carries the 3He vapour from the still, through a hole in every plate above it, to the room-temperature pump. Its conductance matters more than its wall: at 0.8 K the vapour pressure is only a few pascals, so a narrow line would choke the flow and cap the circulation rate that the still heater is trying to raise; hence the wide bore, and a turbo pump backed by a scroll pump or a compressor on the warm end. The tube is thin-walled and thermally anchored at the 4 K and 50 K plates by copper baffles, which also block room-temperature radiation from shining straight down onto the still.",
     "Vapour path from the still to the pumps; conductance limits the flow.", ["still_flow"],
     [row("diameter", "mm", [40, 60]), row("still pressure", "Pa", [1, 20]), row("wall", "mm", [0.3, 0.8]),
      row("3He flow", "mmol/s", binding="cryo.flow.n3", cls="Model"), row("still pressure (live)", "mbar", binding="cryo.flow.p_still", cls="Model")],
     [T08("1.3")], "Ø 50 mm line pumping the still vapour up through every plate.",
     {"generator": "Tube", "r_m": 0.025, "path": "straight_0.8m"}, parent="still")
comp("mixing_chamber", "Mixing chamber", "circulation",
     "Vessel on the mixing-chamber plate where the concentrated 3He arriving from the heat exchangers crosses the phase boundary into the dilute phase (6.6 % 3He in 4He at zero temperature). The enthalpy of the dilute phase is higher, so each mole crossing absorbs 84 T² J, giving the cooling power Q̇ = 84 ṅ₃ T² − Q̇_load: at ṅ₃ = 0.5 mmol/s and 20 mK that is about 17 µW. The chamber is sintered silver inside to keep the Kapitza resistance to the copper body small, and the plate bolted to its base is the coldest surface of the fridge, from which the sample stage and the final attenuators take their temperature.",
     "Enthalpy of mixing across the phase boundary; the cold source of the fridge.", ["cooling_power_mxc", "kapitza"],
     [row("volume", "cm³", [20, 60]), row("dilute-phase 3He fraction", "", [0.06, 0.07]), row("temperature", "K", binding="cryo.stage.mxc.T", cls="Numerical"),
      row("cooling power", "W", binding="cryo.stage.mxc.P_cool", cls="Model"), row("3He flow", "mmol/s", binding="cryo.flow.n3", cls="Model")],
     [T08("1.2"), T08("1.1")], "Mixing chamber: phase boundary of the 3He–4He mixture.",
     {"generator": "Cylinder", "r_m": 0.025, "h_m": 0.06, "caps": True, "attachments": ["flange"], "flange_at": "bottom"}, parent="stage_mxc")
comp("rt_electronics_box", "Room-temperature control electronics", "infra",
     "Enclosure on the frame holding the thermometry bridge, heater drivers and valve controller of the cryostat. The resistance bridge excites each RuO₂ and Cernox thermometer with a few nanoamps to microamps so that its self-heating stays far below the stage's cooling power (a 10 µW excitation would warm a 15 mK mixing chamber by several millikelvin), reads the resistance and converts it through the calibration curve; the heater drivers close the temperature loops on the still and mixing chamber. Keeping this box on the frame rather than in the rack shortens the loom, and its ground is the single-point ground of the insert.",
     "Thermometry readout and heater drive; single-point ground of the insert.", [],
     [row("channels", "", [8, 16]), row("excitation", "A", [1e-9, 1e-5]), row("MXC temperature", "K", binding="cryo.stage.mxc.T", cls="Numerical"),
      row("still heater", "mW", binding="cryo.stage.still.P_heater", cls="Model")],
     [T08("8")], "Thermometry bridge and heater drivers on the frame.",
     {"generator": "Box", "w_m": 0.30, "h_m": 0.20, "d_m": 0.12, "ports": 4}, parent="fridge_frame",
     lod=[{"max_m": 6.0, "detail": "full"}, {"max_m": 30.0, "detail": "simple"}, {"max_m": 1e9, "detail": "hidden"}])

comp("thermal_clamp", "Thermal clamp", "wiring",
     "Copper block bolted to a stage plate that grips the coax outer conductor. Without a clamp the line's outer conductor would run at an intermediate temperature and carry heat straight past the stage into the colder ones; with it, the conducted heat is intercepted here. Clamps are the cheapest and most effective wiring element in the thermal budget.",
     "Anchors the outer conductor to the stage temperature.", ["conduction_load"],
     [row("contact length", "mm", [10, 30]), row("clamp temperature", "K", binding="wiring.line[$line].clamp[$stage].T", cls="Numerical")],
     [T08("4")], "Thermal clamp anchoring a line to this stage.", {"generator": "Box", "w_m": 0.02, "h_m": 0.008, "d_m": 0.015}, parent="wiring")
comp("dc_loom", "DC wiring loom", "wiring",
     "Ribbon of twisted phosphor-bronze pairs carrying thermometer, heater and slow bias signals from the top plate to the mixing chamber, clamped at every stage. Phosphor bronze has low thermal conductivity, so 24 pairs conduct less than one coaxial line. RC filtering at the cold end keeps room-temperature noise off the sensors.",
     "Low-conductivity multi-pair wiring for sensors and heaters.", ["conduction_load"],
     [row("pairs", "", [12, 48]), row("wire diameter", "mm", [0.1, 0.2]), row("conducted heat", "W", binding="wiring.dc.P_cond", cls="Model")],
     [T08("4")], "Twisted-pair loom for thermometry and heaters.", {"generator": "Tube", "r_m": 0.0012, "bundle": 12, "bundle_pitch_m": 0.0026, "path": "top_to_mxc"}, parent="wiring")
comp("isolator", "Cryogenic isolator", "wiring",
     "Ferrite non-reciprocal two-port on the output line at the mixing chamber. It passes the few-photon readout signal toward the amplifier with about 0.5 dB loss but attenuates anything travelling back by 20 dB, so the amplifier's own noise and any reflected pump power do not reach the resonator and dephase the qubit. Two in series give 40 dB of reverse isolation.",
     "Protects the qubit from amplifier back-action and reflected noise.", ["friis"],
     [row("band", "GHz", [4, 8]), row("reverse isolation", "dB", [18, 22]), row("insertion loss", "dB", [0.3, 0.8]), row("temperature", "K", binding="wiring.line[$line].iso[$k].T", cls="Numerical")],
     [T07("8")], "Isolator: one-way passage for the readout signal.", {"generator": "Box", "w_m": 0.03, "h_m": 0.02, "d_m": 0.04, "marking": "arrow"}, parent="stage_mxc")
comp("circulator", "Cryogenic circulator", "wiring",
     "Three-port ferrite device routing the readout probe tone into the feedline and the reflected or transmitted signal out to the amplifier chain, used for reflection-mode readout or to couple a JPA pump. Ports rotate signals cyclically; terminating one port with a 50 Ω load turns it into an isolator.",
     "Separates incoming probe and outgoing signal paths.", [],
     [row("band", "GHz", [4, 8]), row("isolation", "dB", [18, 22]), row("insertion loss", "dB", [0.3, 0.8])],
     [T07("8")], "Circulator: 3-port non-reciprocal router.", {"generator": "Box", "w_m": 0.03, "h_m": 0.02, "d_m": 0.04, "ports": 3}, parent="stage_mxc")
comp("jpa", "Josephson parametric amplifier", "amplifier",
     "Near-quantum-limited first amplifier: a SQUID-terminated resonator pumped at twice the signal frequency amplifies over a 20 MHz band with about 20 dB gain while adding close to the minimum half photon of noise. It sits at the mixing chamber behind the isolators; its narrow band means it is tuned to one feedline's resonators. Pump leakage back to the qubits must be blocked by the isolators.",
     "Quantum-limited pre-amplification setting the measurement efficiency η.", ["quantum_limit", "friis", "readout_snr"],
     [row("gain", "dB", [15, 25], binding="wiring.line[$line].jpa.gain", cls="Model"), row("bandwidth", "MHz", [10, 50]), row("noise temperature", "K", binding="wiring.line[$line].jpa.T_N", cls="Model"), row("pump on", "", binding="wiring.line[$line].jpa.pump_on", cls="Model")],
     [T07("8")], "JPA: quantum-limited narrow-band amplifier.", {"generator": "Box", "w_m": 0.025, "h_m": 0.015, "d_m": 0.03, "ports": ["signal", "pump"]}, parent="stage_mxc")
comp("twpa", "Travelling-wave parametric amplifier", "amplifier",
     "Broadband quantum-limited amplifier built from a long chain of Josephson junctions that amplifies signals co-propagating with a strong pump over about 3 GHz. It lets one output line serve many resonators across the band, at the cost of higher pump power dissipated at the mixing chamber and a noise temperature somewhat above the JPA's.",
     "Broadband near-quantum-limited amplification.", ["quantum_limit", "friis"],
     [row("gain", "dB", [15, 25], binding="wiring.line[$line].jpa.gain", cls="Model"), row("bandwidth", "GHz", [2, 4]), row("noise temperature", "K", binding="wiring.line[$line].jpa.T_N", cls="Model")],
     [T07("8")], "TWPA: broadband parametric amplifier.", {"generator": "Box", "w_m": 0.06, "h_m": 0.012, "d_m": 0.02}, parent="stage_mxc")
comp("pump_line", "Parametric-amplifier pump line", "wiring",
     "Dedicated input line delivering the strong pump tone (tens of dBm at the generator, about −60 dBm at the amplifier) to the JPA or TWPA through a directional coupler. Its 40 dB of attenuation is chosen for pump power rather than qubit noise, and the pump power dissipated at the mixing chamber is a noticeable part of the base-stage budget.",
     "Delivers the pump tone to the parametric amplifier.", ["attn_dissipation"],
     [row("total attenuation", "dB", [30, 50]), row("pump power at amplifier", "dBm", binding="wiring.pump.P", cls="Model")],
     [T07("8")], "Pump line for the parametric amplifier.", {"generator": "Tube", "r_m": 0.00043, "path": "rt_to_jpa"}, parent="wiring")
comp("hemt", "HEMT low-noise amplifier", "amplifier",
     "Cryogenic high-electron-mobility transistor amplifier at the 4 K stage with about 40 dB gain and a noise temperature of 2–4 K over 4–8 GHz. By the Friis formula it fixes the system noise referred to the chip unless a parametric amplifier precedes it. Its 10–20 mW dissipation per line is the dominant load on the 4 K stage in large systems, which is why line counts are budgeted against pulse-tube capacity.",
     "Second-stage amplifier; dominant 4 K heat load and noise floor without a JPA.", ["friis"],
     [row("gain", "dB", [35, 42]), row("noise temperature", "K", [1.5, 4], binding="wiring.line[$line].hemt.T_N", cls="Model"), row("dissipation", "W", [0.008, 0.02], binding="wiring.line[$line].hemt.P_diss", cls="Model")],
     [T07("8")], "HEMT amplifier at 4 K: 40 dB gain, 2–4 K noise.", {"generator": "Box", "w_m": 0.03, "h_m": 0.02, "d_m": 0.05, "marking": "heatsink"}, parent="stage_4K")
comp("directional_coupler", "Directional coupler", "wiring",
     "Four-port passive that couples a fraction (−20 dB) of one line into another with directionality. It injects the JPA pump into the signal path or lets a weak probe be combined with the readout line without a lossy power splitter in the signal path.",
     "Combines pump and signal paths with 20 dB coupling.", ["attn_noise_cascade"],
     [row("coupling", "dB", [-20, -10]), row("directivity", "dB", [15, 25])],
     [T07("9")], "Directional coupler for pump injection.", {"generator": "Box", "w_m": 0.03, "h_m": 0.012, "d_m": 0.03, "ports": 4}, parent="stage_mxc")
comp("bias_tee", "Bias tee", "wiring",
     "Combines a DC or slow flux bias with a fast pulse on one line: the capacitor passes the RF, the inductor passes the DC. Used on flux lines so that a single coax carries both the static operating point of a tunable qubit and its fast CZ pulses.",
     "Merges DC bias and RF pulses onto one line.", ["flux_from_current"],
     [row("RF band", "GHz", [0.01, 12]), row("DC current rating", "mA", [10, 100])],
     [T05("4")], "Bias tee merging DC flux bias with fast pulses.", {"generator": "Box", "w_m": 0.02, "h_m": 0.01, "d_m": 0.02, "ports": 3}, parent="stage_mxc")
comp("rt_amplifier", "Room-temperature amplifier", "amplifier",
     "Final gain stage in the rack before the digitizer, about 30 dB with a 100 K noise temperature. Its noise contribution is divided by the HEMT gain and is negligible; its role is to bring the signal to the digitizer's full-scale range without saturating.",
     "Final gain stage; noise negligible after the HEMT.", ["friis"],
     [row("gain", "dB", [25, 35], binding="wiring.line[$line].rtamp.gain", cls="Model"), row("noise temperature", "K", [80, 150])],
     [T07("8")], "Room-temperature amplifier before the digitizer.", {"generator": "RackUnit", "u": 1, "front": "amp"}, parent="rack_B")
comp("cable_tray", "Cable tray", "structure",
     "Ceiling-mounted tray carrying the bundle of room-temperature coax and control cables between the racks and the fridge top plate. Cable lengths here appear as fixed delays in the pulse schedule and must be matched between drive and readout paths, because a 1 m mismatch is 5 ns of skew, comparable to a gate rise time.",
     "Routes rack cables to the fridge; fixed delay.", [], [row("length", "m", [3, 8]), row("cable delay", "ns", [15, 40])],
     [], "Cable tray from racks to the fridge.", {"generator": "ExtrudedU", "path": "ceiling", "cables": 12}, parent="room")

# ---------------------------------------------------------------- 3.3 rack instruments (also instr::IInstrument, spec 12)
def rack(id, name, cls, u, function, summary, eqs, rows, theory, tooltip, channels, extra_sheet=None, rack_id="A"):
    comp(id, name, "instrument", function, summary, eqs, rows, theory, tooltip,
         {"generator": "RackUnit", "u": u, "front": cls}, parent=f"rack_{rack_id}",
         instrument={"class": cls, "settings_schema": f"instr/{cls}.schema.json", "channels": channels}, model_name=name)
rack("mw_generator", "Microwave signal generator, 20 GHz", "sg_mw", 2,
     "Continuous-wave local oscillator for one drive or readout up-conversion chain. Its frequency sets the carrier the AWG envelopes are mixed onto; its output power sets the mixer drive level; and its phase noise is imprinted on every pulse, appearing as dephasing at the qubit for offsets inside the Rabi bandwidth. All generators are locked to the 10 MHz reference.",
     "Carrier source; phase noise adds to qubit dephasing.", ["dbm_to_watts", "phase_noise", "iq_modulation"],
     [row("frequency", "GHz", [0.01, 20], binding="instr.gen[$i].f", cls="Model"), row("output power", "dBm", [-20, 20], binding="instr.gen[$i].P_dBm", cls="Model"), row("phase noise at 10 kHz", "dBc/Hz", [-120, -100]), row("output on", "", binding="instr.gen[$i].on", cls="Model")],
     [T07("5")], "Microwave generator: local oscillator for one chain.", ["f", "P_dBm", "on"])
rack("control_chassis", "Control chassis (AWG + sequencer)", "awg", 6,
     "Multi-channel arbitrary waveform generator with a hardware sequencer. It plays the I/Q envelopes of every pulse schedule at 1 GS/s with 14-bit resolution, steps through shots, applies real-time phase updates for virtual Z gates, and branches on digitizer results for feed-forward. Timing granularity (16 samples) and minimum pulse length come from its FPGA clock and are compile-time constraints.",
     "Plays pulse schedules; defines timing granularity and feed-forward latency.", ["iq_modulation", "gaussian_pulse", "adc_snr"],
     [row("channels", "", [8, 32]), row("sample rate", "GS/s", [1, 2]), row("resolution", "bit", [14, 16]), row("waveform memory", "MSa", [16, 512]), row("feedback latency", "ns", [150, 300]),
      row("channel waveform", "V", binding="instr.awg.ch[$k].waveform", cls="Exact"), row("running", "", binding="instr.awg.running", cls="Model"), row("current shot", "", binding="instr.seq.shot", cls="Exact")],
     [T07("5"), T07("7")], "AWG and sequencer: plays the compiled pulse schedule.", ["ch[k].waveform", "running", "seq.shot"])
rack("digitizer", "Readout digitizer", "digitizer", 2,
     "Two-channel 1 GS/s analog-to-digital converter that captures the down-converted readout signal, demodulates it at the intermediate frequency with the calibrated integration weights, and returns one (I, Q) point per shot per qubit. The IQ clouds it produces are the physical observable from which state assignment, readout fidelity and the assignment matrix are derived.",
     "Demodulates readout to IQ points; source of all measured bits.", ["heterodyne_demod", "readout_snr", "adc_snr", "readout_confusion"],
     [row("sample rate", "GS/s", [0.5, 2]), row("resolution", "bit", [12, 14]), row("integration window", "µs", [0.2, 2]), row("IQ point", "", binding="instr.dig.ch[$k].iq", cls="Statistical"), row("trigger", "", binding="instr.dig.trigger", cls="Model")],
     [T07("6"), T07("7")], "Digitizer: readout demodulation to IQ points.", ["ch[k].iq", "trigger", "histogram"])
rack("iq_mixer_board", "IQ mixer / up-converter board", "iq_mixer", 1,
     "Single-sideband up-converter that multiplies the AWG I and Q envelopes with the local oscillator to place a pulse at f_LO + f_IF. Imperfections are the calibrated parameters: LO leakage (a carrier at the qubit's neighbourhood even with no pulse) and image sideband from amplitude and phase imbalance. Both are nulled by DC offsets and I/Q skew corrections in the mixer-calibration tool.",
     "Up-conversion; LO leakage and image are calibrated impairments.", ["iq_modulation"],
     [row("RF band", "GHz", [2, 12]), row("LO leakage", "dBc", [-60, -30], binding="instr.mixer[$i].lo_leak", cls="Model"), row("image rejection", "dBc", [-60, -30], binding="instr.mixer[$i].image_rej", cls="Model")],
     [T07("5")], "IQ mixer: up-converts envelopes; leakage and image calibrated.", ["lo_leak", "image_rej"])
rack("dc_source", "Precision DC current source", "dc_source", 3,
     "Low-noise multi-channel current source biasing the flux lines of tunable qubits and couplers. Through the mutual inductance M it sets the flux and thus the qubit frequency; its noise floor and drift appear directly as flux noise, so it is filtered at every stage and often battery-backed. A sweep of one channel produces the flux-spectroscopy arc.",
     "Sets the static flux operating point of tunable elements.", ["flux_from_current", "flux_tunability"],
     [row("channels", "", [8, 64]), row("range", "mA", [-10, 10]), row("resolution", "nA", [1, 10]), row("current", "mA", binding="instr.dc.ch[$k].I", cls="Model"), row("voltage", "V", binding="instr.dc.ch[$k].V", cls="Model")],
     [T05("4")], "DC source: flux bias for tunable qubits and couplers.", ["ch[k].I", "ch[k].V", "sweep"])
rack("vna", "Vector network analyzer, 2-port 9 GHz", "vna", 4, 
     "Measures complex transmission S21 of a readout feedline against frequency. Each resonator appears as a notch whose depth and width give the loaded, internal and coupling quality factors; the notch moves by 2χ when the qubit is excited, which is how the dispersive shift is measured. Power sweeps reveal the punch-out from the dressed to the bare resonator frequency.",
     "Resonator spectroscopy: f_r, Q_i, Q_c, and 2χ from S21.", ["vna_s21_notch", "q_loaded", "dispersive_shift"],
     [row("frequency range", "GHz", [0.01, 9]), row("dynamic range", "dB", [100, 130]), row("IF bandwidth", "Hz", [1, 100000]), row("trace", "", binding="instr.vna.trace", cls="Model"), row("span", "GHz", binding="instr.vna.span", cls="Model"), row("power", "dBm", binding="instr.vna.P", cls="Model")],
     [T05("6.3"), T07("6")], "VNA: S21 of the feedline; resonator fits.", ["s21", "s21_phase", "circle"], rack_id="B")
rack("spectrum_analyzer", "Spectrum analyzer, 26 GHz", "spectrum_analyzer", 4,
     "Displays the power spectrum of an AWG or mixer output at the resolution bandwidth chosen. It is how mixer calibration is verified (carrier leakage and image sideband as separate lines), how a DRAG pulse's sideband suppression is checked, and how spurs from the generator are found before they land on a qubit transition.",
     "Power spectrum of control signals; verifies mixer calibration and pulse spectra.", ["iq_modulation", "gaussian_pulse"],
     [row("frequency range", "GHz", [0.001, 26]), row("resolution bandwidth", "kHz", [0.001, 3000]), row("displayed noise floor", "dBm/Hz", [-160, -140]), row("trace", "", binding="instr.sa.trace", cls="Model")],
     [T07("5")], "Spectrum analyzer for control-signal spectra.", ["trace", "markers"], rack_id="B")
rack("oscilloscope", "Oscilloscope, 4 GHz", "oscilloscope", 4,
     "Time-domain view of AWG envelopes, trigger timing and the demodulated readout signal. Used to align channel delays (cable lengths differ by tens of nanoseconds), to verify pulse shapes and rise times, and to inspect the raw readout transient before integration.",
     "Time-domain inspection of pulses and readout transients.", ["gaussian_pulse", "heterodyne_demod"],
     [row("bandwidth", "GHz", [1, 4]), row("sample rate", "GS/s", [5, 20]), row("channels", "", [2, 4]), row("channel trace", "V", binding="instr.scope.ch[$k]", cls="Exact")],
     [T07("7")], "Oscilloscope for pulse and readout waveforms.", ["ch[k]"], rack_id="B")
rack("ref_10mhz", "10 MHz frequency reference", "controller", 1,
     "Oven-controlled or rubidium 10 MHz reference that every generator, AWG and digitizer locks to. Without a common reference the frames of drive and readout drift apart and virtual-Z phases become meaningless within milliseconds; the lock indicator is a precondition for any run.",
     "Common frequency reference for all instruments.", [], [row("stability", "", [1e-11, 1e-9]), row("locked", "", binding="instr.ref.locked", cls="Model")],
     [T07("5")], "10 MHz reference: phase coherence between instruments.", ["locked"], rack_id="B")
rack("clock_dist", "Clock distribution", "controller", 1,
     "Fans the 10 MHz reference and the sample clocks out to every instrument with matched cable lengths so that channels start on the same edge. Skew between channels shows up as a fixed phase offset between qubits and is calibrated out by the compiler's per-channel delay table.",
     "Distributes reference and sample clocks.", [], [row("outputs", "", [8, 16]), row("skew", "ps", [0, 100])],
     [T07("7")], "Clock distribution amplifier.", [], rack_id="B")
rack("trigger_unit", "Trigger and sequencing unit", "controller", 1,
     "Generates the shot-repetition trigger and gates the digitizer acquisition window relative to the AWG start. The repetition rate here is the control-system overhead term in the wall-time model: each shot cannot begin before the previous readout, reset and trigger re-arm complete.",
     "Shot repetition trigger; sets the per-shot gap.", ["wall_time"], [row("trigger rate", "kHz", [1, 1000], binding="instr.trig.rate", cls="Model"), row("jitter", "ps", [0, 50])],
     [T07("7")], "Trigger unit: shot repetition and acquisition gating.", ["rate"], rack_id="B")
comp("power_dist", "Rack power distribution", "infra", "Switched and filtered mains distribution for one rack with total-power monitoring. It is included because instrument power (about 2 kW per rack) is a real load on the laboratory's cooling and because a filtered feed reduces mains-borne noise into the DC sources.",
     "Mains distribution; no physics role.", [], [row("total power", "W", binding="instr.rack.P_total", cls="Model"), row("outlets", "", [8, 16])], [], "Rack power distribution unit.", {"generator": "RackUnit", "u": 1, "front": "pdu"}, parent="rack_A")
# ------------------------------------------------- scenery with descriptors (spec 17 §1, §12)
# These render as part of the room but had no descriptor, so they were drawn and never pickable.
# Giving them one makes every visible node inspectable.
comp("rack_enclosure", "19-inch instrument rack", "infra",
     "Welded 19-inch rack, 42 U high, that carries the room-temperature control electronics and presents them to the fridge through one patch panel. The rack is more than furniture: its bonded frame is the single-point ground for every chassis in it, so ground loops between instruments close here rather than through the coaxial screens, and its front-to-back airflow sets the temperature of the generators whose phase noise drifts with it. Rack A holds the control chassis, microwave sources, digitizer and mixer boards; rack B holds the test instruments and room-temperature amplifiers.",
     "Single-point ground and thermal environment for the control electronics.", [],
     [row("height", "U", [42, 42]), row("usable depth", "mm", [800, 1000]),
      row("airflow", "m^3/h", [200, 600]), row("ground bond", "mohm", [0, 10])],
     [], "Instrument rack: mechanical support, single-point ground and airflow.",
     {"generator": "Box", "w_m": 0.6, "h_m": 2.0, "d_m": 0.9}, lod=[{"max_m": 6.0, "detail": "full"}, {"max_m": 30.0, "detail": "simple"}, {"max_m": 1e9, "detail": "hidden"}], parent="room")

comp("workstation", "Control workstation", "infra",
     "The classical computer that runs QuantumXLab itself: it compiles the program, uploads waveforms to the control chassis, receives the demodulated shot records and stores the calibration set. Its latency matters only for feed-forward experiments, where the decision must return to the chassis inside the qubit coherence time; everything else is batch traffic over the control link. Nothing in the quantum stack depends on its clock, which is why the timing reference is a separate rack unit.",
     "Classical control host; no role in the quantum signal path.", [],
     [row("control link", "Gb/s", [1, 10]), row("feed-forward latency", "us", [1, 100]),
      row("storage per run", "MB", [1, 500])],
     [], "Workstation: compiles programs and stores results.",
     {"generator": "Box", "w_m": 1.6, "h_m": 0.75, "d_m": 0.8}, lod=[{"max_m": 6.0, "detail": "full"}, {"max_m": 30.0, "detail": "simple"}, {"max_m": 1e9, "detail": "hidden"}], parent="room")

comp("bench", "Assembly bench", "infra",
     "Static-safe bench where the sample package is wirebonded and the fridge wiring is assembled and tested before it is installed. Work done here determines much of the machine's performance: a bond wire with the wrong loop height adds inductance in series with the drive line, and a coaxial connector torqued incorrectly changes its attenuation by a fraction of a decibel, which shifts the photon number reaching the qubit.",
     "Assembly and inspection; sets wiring quality before installation.", [],
     [row("surface resistance", "ohm", [1e6, 1e9]), row("working area", "m^2", [1, 3])],
     [], "Bench: sample packaging and wiring assembly.",
     {"generator": "Box", "w_m": 1.8, "h_m": 0.9, "d_m": 0.8}, lod=[{"max_m": 6.0, "detail": "full"}, {"max_m": 30.0, "detail": "simple"}, {"max_m": 1e9, "detail": "hidden"}], parent="room")

comp("he_dewar", "Liquid-helium dewar", "infra",
     "Storage dewar for the liquid helium used to pre-cool the cryostat and to top up the mixture reservoir. A cryogen-free system needs it only during maintenance, which is the point: the pulse tube replaced a standing helium supply, and the dewars remain for the rare occasions when the mixture must be recovered or the insert warmed and re-cooled quickly. Boil-off is continuous, so a dewar left connected is a slow leak of an expensive gas.",
     "Cryogen storage for pre-cooling and mixture recovery.", [],
     [row("capacity", "L", [100, 250]), row("boil-off", "%/day", [0.5, 2.0]),
      row("helium price", "USD/L", [10, 50])],
     [], "Helium dewar: pre-cooling and mixture recovery.",
     {"generator": "Cylinder", "r_m": 0.25, "h_m": 1.3}, lod=[{"max_m": 6.0, "detail": "full"}, {"max_m": 30.0, "detail": "simple"}, {"max_m": 1e9, "detail": "hidden"}], parent="room")

comp("pt_compressor", "Pulse-tube compressor", "cryocooler",
     "Helium compressor that drives the pulse-tube cryocooler, standing outside the lab because it dissipates several kilowatts and is the loudest object in the system. It supplies high-pressure helium through flexible lines to the rotary valve on the cold head; the valve switches the gas between high and low pressure at about 1.4 Hz, and that cycle is both the cooling power of the 45 K and 4 K stages and the dominant mechanical vibration reaching the qubits. Stopping it starts the warm-up.",
     "Supplies the pressure wave that produces the 45 K and 4 K cooling power (T08).",
     [], [row("input power", "kW", [5, 12]), row("helium pressure", "bar", [16, 22]),
          row("cycle frequency", "Hz", [1.0, 2.0]), row("cooling water", "L/min", [5, 15]),
          row("sound level", "dB(A)", [65, 80])],
     [T08("2")], "Pulse-tube compressor: 1.4 Hz pressure wave, kilowatts of input power.",
     {"generator": "Box", "w_m": 0.7, "h_m": 1.0, "d_m": 0.8}, lod=[{"max_m": 6.0, "detail": "full"}, {"max_m": 30.0, "detail": "simple"}, {"max_m": 1e9, "detail": "hidden"}], parent="room")

comp("patch_panel", "Patch panel", "infra", "SMA bulkhead panel where the fridge lines terminate at the rack. Every line is labelled with its fridge feedthrough and its function; the compiler's channel-to-port map follows this panel, so a swapped cable here is the classic cause of driving the wrong qubit.",
     "Physical line-to-channel mapping.", [], [row("ports", "", [24, 96])], [], "Patch panel: line-to-channel mapping.", {"generator": "RackUnit", "u": 2, "front": "patch"}, parent="rack_B")

# ---------------------------------------------------------------- 3.4 gas handling system
comp("ghs_cabinet", "Gas handling system", "ghs",
     "Cabinet holding the pumps, valves, gauges and traps that store, circulate, and clean the 3He/4He mixture. Its mimic panel is the operator's view of the fridge state machine: pumping the vacuum can, pre-cooling with the pulse tube, condensing the mixture, running at base, and warming up. Every valve state below is a node of that state machine.",
     "Stores and circulates the helium mixture; hosts the cooldown state machine.", ["still_flow"],
     [row("mixture volume (STP)", "L", [20, 60]), row("3He fraction", "", [0.2, 0.3]), row("state", "", binding="cryo.ghs.state", cls="Model")],
     [T08("1.3")], "Gas handling cabinet with the mixture circuit.", {"generator": "Box", "w_m": 0.8, "h_m": 1.9, "d_m": 0.7, "front": "valve_mimic"}, parent="room")
comp("turbo_pump", "Turbomolecular pump", "ghs",
     "Backed by the scroll pump, it evacuates the outer vacuum can to below 1e-5 mbar before cooldown. Once cold, cryopumping on the 4 K surfaces maintains the vacuum and the turbo is valved off; its speed and the OVC pressure are the two numbers watched during the first hours of a cooldown.",
     "Evacuates the insulating vacuum.", ["turbo_pressure"],
     [row("pumping speed", "L/s", [60, 300]), row("rotor speed", "rpm", binding="cryo.pump.turbo.rpm", cls="Model"), row("OVC pressure", "mbar", binding="cryo.ovc.pressure", cls="Model")],
     [T08("5")], "Turbo pump for the outer vacuum can.", {"generator": "Cylinder", "r_m": 0.06, "h_m": 0.15, "caps": True, "attachments": ["flange"]}, parent="ghs_cabinet")
comp("scroll_pump", "Scroll pump", "ghs",
     "Dry backing pump for the turbo and for the still during circulation. An oil-free pump is essential because oil vapour would contaminate the mixture and block the condensing impedance within days. Its exhaust returns the 3He to the compressor or the dump tanks depending on the valve configuration.",
     "Backing and still pumping; oil-free.", [], [row("pumping speed", "m³/h", [10, 35]), row("running", "", binding="cryo.pump.scroll.on", cls="Model")],
     [T08("5")], "Dry scroll pump backing the turbo and still.", {"generator": "Box", "w_m": 0.3, "h_m": 0.3, "d_m": 0.5}, parent="ghs_cabinet")
comp("he3_compressor", "Mixture circulation compressor", "ghs",
     "Hermetic compressor that raises the still exhaust (a few mbar) to the condensing pressure (a few bar) so that the 3He can be re-condensed at 4 K. In cryogen-free systems the circulation rate it delivers, together with the still power, sets the mixing-chamber cooling power.",
     "Circulates 3He from the still back to the condenser.", ["still_flow", "cooling_power_mxc"],
     [row("throughput", "mmol/s", [0.5, 3], binding="cryo.flow.n3", cls="Model"), row("still pressure", "mbar", binding="cryo.flow.p_still", cls="Model")],
     [T08("1.3")], "Compressor circulating the 3He.", {"generator": "Box", "w_m": 0.4, "h_m": 0.4, "d_m": 0.5}, parent="ghs_cabinet")
comp("ln2_trap", "Liquid-nitrogen cold trap", "ghs",
     "Charcoal-filled coil immersed in liquid nitrogen through which the circulating mixture passes before re-entering the fridge. It adsorbs air, water and oil that would otherwise freeze in the condensing impedance and block circulation, the most common cause of a slowly failing base temperature.",
     "Cleans the circulating gas of contaminants.", [], [row("trap temperature", "K", binding="cryo.trap.T", cls="Model"), row("LN2 level", "", binding="cryo.trap.level", cls="Model")],
     [T08("1.3")], "LN2 cold trap cleaning the mixture.", {"generator": "Cylinder", "r_m": 0.12, "h_m": 0.4, "caps": True, "attachments": ["coil"]}, parent="ghs_cabinet")
comp("pressure_gauge", "Pressure gauge", "sensor",
     "Pirani, capacitance or piezo gauge on one node of the gas circuit (vacuum can, still, condensing line, dump tanks). Together the gauges give the operator the state of the circulation: a rising still pressure with falling flow means a blockage; a rising can pressure means a leak or warming shields.",
     "Pressure at one node of the gas circuit.", [], [row("range", "mbar", [1e-6, 3000]), row("pressure", "mbar", binding="cryo.gauge[$i].p", cls="Model")],
     [T08("5")], "Pressure gauge on the gas circuit.", {"generator": "Cylinder", "r_m": 0.03, "h_m": 0.02, "caps": True, "front": "dial"}, parent="ghs_cabinet",
     instrument={"class": "pressure_gauge", "settings_schema": "instr/gauge.schema.json", "channels": ["p"]}, model_name="Wide-range vacuum gauge")
comp("flow_meter", "3He flow meter", "sensor",
     "Thermal mass-flow meter in the circulation loop reporting the 3He molar flow. It is the ṅ₃ in the cooling-power law, so the fridge dashboard shows it next to the mixing-chamber temperature and the cooling-power margin; a falling flow at constant still power is the signature of a blocked impedance.",
     "Measures the circulation rate ṅ₃.", ["cooling_power_mxc"], [row("range", "mmol/s", [0, 5]), row("flow", "mmol/s", binding="cryo.flow.n3", cls="Model")],
     [T08("1.2")], "Flow meter for the 3He circulation.", {"generator": "Tube", "r_m": 0.01, "path": "straight_0.1m"}, parent="ghs_cabinet",
     instrument={"class": "flow_meter", "settings_schema": "instr/flow.schema.json", "channels": ["n3"]}, model_name="Thermal mass-flow meter")
comp("power_meter", "RF power meter", "instrument",
     "Diode or thermistor power head with its readout, used on the bench to set the absolute power leaving a generator or arriving at a fridge input before anything is connected to the qubit. It is the only instrument in the lab that measures power on an absolute scale rather than a ratio, so it is what anchors the attenuation budget: every dBm quoted for a drive line traces back to a head like this one. Calibration factor and frequency response are entered per head, and the reading is averaged over many RF cycles, so it reports average power, not the envelope.",
     "Absolute average RF power; anchors the line attenuation budget (T07).",
     ["attn_noise_cascade"],
     [row("frequency range", "GHz", [0.01, 18]), row("power range", "dBm", [-70, 20]),
      row("uncertainty", "dB", [0.1, 0.5]), row("averaging time", "ms", [1, 1000]),
      row("reading", "dBm", binding="instr.power_meter.p", cls="Model")],
     [T07("9")], "RF power meter: absolute average power, the reference for the dBm budget.",
     {"generator": "Box", "w_m": 0.12, "h_m": 0.05, "d_m": 0.16}, parent="bench",
     instrument={"class": "power_meter", "settings_schema": "instr/power_meter.schema.json",
                 "channels": ["p"]}, model_name="Thermistor RF power head and readout")
comp("valve", "Circuit valve", "ghs",
     "Pneumatic or manual valve on the gas circuit. The set of open valves defines the flow path: OVC pumping, mixture condensing, circulation, or recovery into the dump tanks. The sequencer opens and closes them in the order that never exposes the mixture to atmosphere or the impedance to contaminants.",
     "One switch of the gas-circuit topology.", [], [row("open", "", binding="cryo.valve[$i].open", cls="Model")],
     [T08("1.3")], "Valve on the gas circuit.", {"generator": "Cylinder", "r_m": 0.015, "h_m": 0.02, "caps": True, "attachments": ["handle"]}, parent="ghs_cabinet")
comp("dump_tank", "Mixture dump tank", "ghs",
     "Storage vessel holding the helium mixture at a few bar when the fridge is warm. The mixture is the most expensive consumable of the system (3He), so recovery into the dumps is done before any warm-up and the dump pressure is logged as an inventory check.",
     "Stores the mixture when not circulating.", [], [row("volume", "L", [20, 60]), row("pressure", "bar", binding="cryo.dump[$i].p", cls="Model")],
     [T08("1.3")], "Mixture storage tank.", {"generator": "Cylinder", "r_m": 0.15, "h_m": 0.6, "caps": True}, parent="ghs_cabinet")

# ---------------------------------------------------------------- 3.5 chip
def chip(id, name, cat, function, summary, eqs, rows, theory, tooltip, geometry, parent="substrate"):
    comp(id, name, cat, function, summary, eqs, rows, theory, tooltip, geometry, parent=parent,
         lod=[{"max_m": 0.02, "detail": "full"}, {"max_m": 0.2, "detail": "simple"}, {"max_m": 1e9, "detail": "hidden"}])
chip("substrate", "Chip substrate", "chip",
     "High-resistivity silicon (or sapphire) die on which the aluminium or niobium circuit is patterned. Its dielectric loss tangent and the two-level-system defects at its surfaces and interfaces set the internal quality factor of resonators and one of the T1 limits of the qubits; its permittivity sets the coplanar-waveguide phase velocity and hence every resonator length.",
     "Dielectric host; loss and permittivity of the whole circuit.", ["resonator_length"],
     [row("size", "mm", [5, 10]), row("thickness", "mm", [0.3, 0.5]), row("relative permittivity", "", [9.4, 11.9]), row("temperature", "K", binding="device.T_sample", cls="Numerical")],
     [T05("1"), T05("10")], "Silicon or sapphire substrate of the chip.", {"generator": "Box", "w_m": 0.008, "h_m": 0.00035, "d_m": 0.008}, parent="chip_package")
chip("ground_plane", "Superconducting ground plane", "chip",
     "Continuous superconducting film covering the die except where qubits, resonators and lines are etched. It provides the return path of every coplanar waveguide; slots in it must be stitched with airbridges or bumps so that spurious slot-line modes do not couple qubits to each other across the chip.",
     "Return conductor for all coplanar structures.", [], [row("film", "", [0, 0]), row("thickness", "nm", [100, 200])],
     [T05("6.3")], "Ground plane film with etched circuit.", {"generator": "PlateWithHoles", "from": "chip_layout"})
chip("transmon_pad", "Transmon capacitor (Xmon cross)", "qubit",
     "The shunt capacitor of one transmon: a cross or pad pair whose capacitance to ground C_Σ sets the charging energy E_C and, with the junction's E_J, the qubit frequency and anharmonicity. Its geometry also sets the coupling capacitances to the readout resonator, the neighbours and the drive line; the surface participation of its edges is where most dielectric loss lives.",
     "Sets E_C, the qubit frequency and all capacitive couplings.", ["transmon_hamiltonian", "transmon_frequency", "duffing", "bloch_vector"],
     [row("arm length", "µm", [100, 300]), row("charging energy E_C/h", "MHz", [200, 350], binding="device.qubit[$i].EC", cls="Model"), row("f01", "GHz", binding="device.qubit[$i].f01", cls="Model"),
      row("T1", "µs", binding="device.qubit[$i].T1", cls="Model"), row("T2", "µs", binding="device.qubit[$i].T2", cls="Model"), row("excited population", "", binding="device.qubit[$i].pop_e", cls="Model"),
      row("Bloch vector", "", binding="device.qubit[$i].bloch", cls="Exact"), row("drive amplitude", "MHz", binding="device.qubit[$i].drive_amp", cls="Model")],
     [T05("3")], "Transmon: the qubit's shunt capacitor.", {"generator": "Xmon", "from": "device.qubit[$i]"})
chip("junction", "Josephson junction", "qubit",
     "Aluminium/aluminium-oxide/aluminium tunnel junction of about 100×100 nm² forming the non-linear inductor of the transmon. Its critical current sets E_J; because the qubit frequency goes as √(8E_J E_C), a 1 % spread in junction resistance across a wafer becomes a 0.5 % (25 MHz) spread in frequency, which is why frequency plans need collision checks.",
     "Non-linear inductance; E_J from the critical current.", ["josephson_relations", "josephson_inductance"],
     [row("area", "µm²", [0.01, 0.05]), row("Josephson energy E_J/h", "GHz", binding="device.qubit[$i].EJ", cls="Model"), row("external flux", "Φ0", binding="device.qubit[$i].phi_ext", cls="Model")],
     [T05("2")], "Josephson junction: the transmon's non-linear inductor.", {"generator": "Junction", "w_lead_um": 1.0, "overlap_um": 0.15})
chip("squid_loop", "SQUID loop", "qubit",
     "Two junctions in a loop, giving a flux-tunable effective E_J. Applying flux through the loop moves the qubit frequency by up to a gigahertz, enabling flux-activated CZ gates and frequency parking, at the price of first-order flux-noise sensitivity away from the sweet spot at integer flux quanta.",
     "Flux tunability of E_J; sweet spot at Φ = 0.", ["flux_tunability", "flux_from_current"],
     [row("loop area", "µm²", [20, 200]), row("asymmetry d", "", [0, 0.3]), row("external flux", "Φ0", binding="device.qubit[$i].phi_ext", cls="Model"), row("f01", "GHz", binding="device.qubit[$i].f01", cls="Numerical")],
     [T05("4")], "SQUID loop: flux-tunable junction pair.", {"generator": "SquidLoop", "area_um2": 60, "w_um": 1.0})
chip("readout_resonator", "Readout resonator (λ/4 CPW)", "resonator",
     "Quarter-wave coplanar meander capacitively coupled at its open end to one transmon and inductively or capacitively to the feedline. In the dispersive regime its frequency shifts by ±χ with the qubit state; probing it near resonance and integrating the reflected phase is the measurement. Its linewidth κ trades readout speed against Purcell decay.",
     "State-dependent frequency shift is the readout mechanism.", ["dispersive_shift", "readout_optimum", "resonator_length", "vna_s21_notch", "q_loaded"],
     [row("frequency", "GHz", binding="device.res[$i].f_r", cls="Model"), row("linewidth κ/2π", "MHz", binding="device.res[$i].kappa", cls="Model"), row("dispersive shift χ/2π", "MHz", binding="device.res[$i].chi", cls="Model"),
      row("photon number", "", binding="device.res[$i].n_photons", cls="Numerical"), row("S21", "", binding="device.res[$i].S21", cls="Model")],
     [T05("6.3")], "λ/4 readout resonator coupled to one qubit.", {"generator": "Meander", "from": "device.res[$i]", "w_um": 10, "s_um": 6})
chip("feedline", "Readout feedline", "resonator",
     "Through coplanar waveguide crossing the chip, past which up to eight readout resonators hang. One input line and one output line serve all of them by frequency multiplexing; the probe tones for every qubit on the feedline travel down it together and the transmitted signal carries all their resonator responses.",
     "Multiplexed readout bus for up to eight resonators.", ["vna_s21_notch"],
     [row("resonators per feedline", "", [4, 10]), row("probe power at chip", "dBm", [-135, -115], binding="wiring.readout.P", cls="Model")],
     [T05("6.3")], "Feedline: multiplexed readout bus.", {"generator": "Cpw", "from": "chip_layout.feedline[$i]", "w_um": 10, "s_um": 6})
chip("purcell_filter", "Purcell filter", "resonator",
     "Band-pass section at the feedline input, tuned to the readout band, that presents the qubit frequency with a high impedance. It lets the readout resonators have a large κ for fast measurement without the qubit decaying through them at the Purcell rate κ g²/Δ².",
     "Suppresses Purcell decay while allowing large κ.", ["purcell_decay"],
     [row("centre frequency", "GHz", binding="device.pf[$i].f", cls="Model"), row("bandwidth", "MHz", [50, 300])],
     [T05("6.4")], "Purcell filter protecting qubits from feedline decay.", {"generator": "Cpw", "from": "chip_layout.purcell[$i]", "w_um": 10, "s_um": 6})
chip("coupler_fixed", "Fixed capacitive coupler", "coupler",
     "Short coplanar segment or interdigitated capacitor giving a fixed exchange coupling J between two neighbouring transmons. With fixed-frequency qubits it is the medium of the cross-resonance gate; its size also fixes the always-on ZZ interaction that must be echoed away during idles.",
     "Fixed exchange coupling J; source of static ZZ.", ["capacitive_coupling", "zz_interaction", "cross_resonance"],
     [row("coupling J/2π", "MHz", [2, 6], binding="device.edge[$e].J", cls="Model"), row("ZZ", "kHz", binding="device.edge[$e].zz", cls="Model")],
     [T05("5.1"), T05("8")], "Fixed coupler between two transmons.", {"generator": "Cpw", "from": "chip_layout.edge[$e]", "w_um": 10, "s_um": 6})
chip("coupler_tunable", "Tunable coupler", "coupler",
     "Transmon-like element between two qubits whose frequency, set by its own flux line, controls the effective coupling. Parked high it cancels the direct coupling to a few kHz of residual ZZ; pulsed toward the qubits it switches on a strong interaction for a 30–60 ns CZ or √iSWAP.",
     "Switchable coupling with a zero-coupling idle point.", ["tunable_coupler", "zz_interaction", "cz_phase"],
     [row("idle frequency", "GHz", binding="device.edge[$e].f_c", cls="Model"), row("effective coupling", "MHz", binding="device.edge[$e].g_eff", cls="Model"), row("residual ZZ", "kHz", binding="device.edge[$e].zz", cls="Model")],
     [T05("5.2"), T05("9")], "Tunable coupler with its own flux control.", {"generator": "Xmon", "from": "device.edge[$e].coupler", "scale": 0.6})
chip("drive_line", "XY drive line", "line",
     "Coplanar line ending in a small capacitor to the transmon pad. Its coupling ratio β = C_d/C_Σ is deliberately weak so that the 50 Ω line does not become a T1 channel, which is why tens of dBm at the generator become a few MHz of Rabi frequency after 60 dB of attenuation.",
     "Weakly coupled drive port; converts line voltage to Rabi frequency.", ["rabi_from_power", "drive_hamiltonian"],
     [row("coupling capacitance", "fF", [0.05, 0.3]), row("Rabi frequency at full amplitude", "MHz", binding="device.qubit[$i].drive_amp", cls="Model")],
     [T07("3")], "Drive line to one qubit.", {"generator": "Cpw", "from": "chip_layout.drive[$i]", "w_um": 10, "s_um": 6})
chip("flux_line", "Flux bias line", "line",
     "Coplanar line short-circuited to ground next to the SQUID loop so that its current threads flux through the loop with mutual inductance M of about 2 pH. Both the static bias from the DC source and fast flux pulses from the AWG arrive here; the line's low-pass filtering sets the fastest CZ pulse.",
     "Flux control of a tunable element via mutual inductance M.", ["flux_from_current", "flux_tunability"],
     [row("mutual inductance", "pH", [1, 5]), row("external flux", "Φ0", binding="device.qubit[$i].phi_ext", cls="Model")],
     [T05("4")], "Flux line threading the SQUID loop.", {"generator": "Cpw", "from": "chip_layout.flux[$i]", "w_um": 10, "s_um": 6})
chip("airbridge", "Airbridge", "chip",
     "Free-standing aluminium arch connecting the two ground planes across a coplanar gap. Every 200 μm along every line and at every crossing, bridges keep both grounds at the same potential and kill the parasitic slot-line mode that would otherwise couple distant qubits.",
     "Ground stitching across CPW gaps; suppresses slot-line modes.", [], [row("pitch", "µm", [100, 300]), row("height", "µm", [2, 4])],
     [T05("6.3")], "Airbridge stitching the ground planes.", {"generator": "Airbridge", "span_um": 30, "w_um": 10, "h_um": 3})
chip("bond_pad", "Bond pad and wirebond", "chip",
     "Large pad at the chip edge with an aluminium wirebond arcing to the PCB launcher. The bond's inductance of about 1 nH per millimetre is the main impedance discontinuity at the chip boundary; several bonds in parallel are used on the readout lines to keep reflections low.",
     "Chip-to-PCB transition; bond inductance limits matching.", [], [row("pitch", "µm", [150, 250]), row("bond inductance", "nH", [0.5, 2])],
     [T05("6.3")], "Bond pad with wirebond to the PCB.", {"generator": "WirebondArc", "h_um": 300})
chip("flip_chip_bump", "Flip-chip indium bump", "chip",
     "Indium sphere bonding a qubit die to a wiring die face-to-face. Bumps carry ground and signals between the two chips and set their 5–10 μm separation; used instead of wirebonds when the routing density of a large lattice exceeds what one layer allows.",
     "Vertical interconnect for two-die packaging.", [], [row("pitch", "µm", [50, 200]), row("gap", "µm", [5, 10])],
     [T05("6.3")], "Indium bump for flip-chip packaging.", {"generator": "Sphere", "r_um": 10})
chip("package_cavity", "Package cavity mode", "chip",
     "The electromagnetic mode of the enclosure around the chip. It is not a fabricated part but the volume between lid and PCB; its frequency must sit above the qubit and readout bands, and the inspector shows where it lies relative to them because a low cavity mode couples all qubits together and adds Purcell loss.",
     "Enclosure mode that must lie above the operating band.", ["purcell_decay"], [row("frequency", "GHz", binding="device.package.f_cavity", cls="Model")],
     [T05("6.4")], "Package cavity: enclosure mode above the band.", {"generator": "Box", "from": "chip_package.cavity", "wireframe": True}, parent="chip_package")

# ---------------------------------------------------------------- 3.6 trapped-ion device (reduced detail)
def ion(id, name, function, summary, eqs, rows, theory, tooltip, geometry, parent="vacuum_chamber"):
    comp(id, name, "ion_device", function, summary, eqs, rows, theory, tooltip, geometry, parent=parent)
ion("vacuum_chamber", "Ultra-high-vacuum chamber", "Stainless octagon with viewports holding the trap at 1e-11 mbar. Collisions with background gas eject or heat ions, so pressure sets the chain lifetime; the chamber is baked and ion-pumped and often cooled to 4 K in newer systems to lower pressure further.",
    "UHV environment; pressure sets chain lifetime.", [], [row("pressure", "mbar", [1e-12, 1e-10]), row("viewports", "", [6, 10])], [T06("1")], "UHV chamber holding the ion trap.", {"generator": "Octagon", "r_m": 0.12, "h_m": 0.1}, parent="room")
ion("trap_chip", "Surface-electrode Paul trap", "Microfabricated electrode chip whose RF rails create the pseudopotential and whose DC segments shape the axial well that holds the chain. Ion positions (glowing spheres) and their Bloch vectors are bound here; the radial and axial secular frequencies come from the RF amplitude and DC voltages.",
    "RF pseudopotential and DC confinement of the chain.", ["paul_pseudopotential", "chain_equilibrium", "normal_modes"],
    [row("RF frequency", "MHz", [20, 50]), row("axial frequency", "MHz", binding="device.trap.omega_z", cls="Numerical"), row("ion Bloch vector", "", binding="device.ion[$i].bloch", cls="Exact")],
    [T06("1.1"), T06("1.3")], "Paul trap chip with DC and RF electrodes.", {"generator": "TrapChip", "electrodes": 40})
ion("laser_path", "Laser beam path", "Beam delivering one wavelength to the chain: 369 nm for Doppler cooling and fluorescence detection of Yb+, 355 nm pulsed Raman beams for gates, 935 nm repump. Individual-addressing beams are focused to a few micrometres per ion; the global beam covers the chain.",
    "Optical control and detection of the ions.", ["lamb_dicke", "ms_hamiltonian"], [row("wavelength", "nm", [355, 935]), row("beam on", "", binding="instr.laser[$i].on", cls="Model")],
    [T06("3")], "Laser beam path to the trap.", {"generator": "Cylinder", "r_m": 0.001, "h_m": 0.3, "colour_from": "wavelength"})
ion("imaging_objective", "Imaging objective", "High-numerical-aperture lens collecting fluorescence from each ion onto the detector array. Collection efficiency (a few per cent) and detection time together set the state-detection error through the Poisson statistics of the collected photons; a longer window lowers the error until off-resonant pumping starts to flip the state.",
    "Collects fluorescence for state detection.", ["fluorescence_threshold"], [row("numerical aperture", "", [0.3, 0.6]), row("collection efficiency", "", [0.01, 0.05])],
    [T06("7.1")], "Objective collecting ion fluorescence.", {"generator": "Cylinder", "r_m": 0.02, "h_m": 0.05, "caps": True})
ion("pmt_camera", "Photon-counting detector array", "PMT array or EMCCD camera counting fluorescence photons per ion during the detection window. The bright and dark count distributions and the threshold between them define the readout error, typically a few 1e-3 after 100–300 µs; the per-ion bright flag from the last detection is bound here.",
    "Photon counting for state readout.", ["fluorescence_threshold"], [row("detection time", "µs", [100, 1000]), row("bright", "", binding="device.ion[$i].bright", cls="Statistical")],
    [T06("7.1")], "PMT/camera detector for ion fluorescence.", {"generator": "Box", "w_m": 0.05, "h_m": 0.05, "d_m": 0.08})
ion("helical_resonator", "Helical RF resonator", "Step-up resonator that raises the RF drive to the hundreds of volts needed on the trap rails while filtering amplitude noise; its quality factor and the RF amplitude stability determine the radial secular frequency stability, and through the mode frequencies the fidelity of every Mølmer–Sørensen gate.",
    "RF voltage step-up and filtering for the trap.", ["paul_pseudopotential"], [row("resonant frequency", "MHz", [20, 50]), row("RF amplitude", "V", binding="instr.rf.V", cls="Model")],
    [T06("1.1")], "Helical resonator driving the trap RF.", {"generator": "Cylinder", "r_m": 0.05, "h_m": 0.2, "caps": True})
ion("magnetic_coils", "Quantization-field coils", "Coil pair producing the few-gauss field that defines the quantization axis and splits the Zeeman levels. Clock-state qubits are first-order insensitive to it, which is why hyperfine clock qubits reach coherence times of seconds; field noise still limits the coherence of Zeeman-sensitive transitions used for some gates and for state preparation.",
    "Defines the quantization axis.", [], [row("field", "G", [3, 10])], [T06("2")], "Coils for the quantization field.", {"generator": "Torus", "r_m": 0.15, "tube_r_m": 0.01, "count": 2})

# ================================================================ layouts (spec 17 §2, §11)
def layouts():
    fridge_stage_y = {"rt": 2.30, "s50": 2.05, "s4": 1.80, "still": 1.55, "cp": 1.40, "mxc": 1.25}  # metres above floor
    sc = {
        "id": "sc_lab_standard",
        "room": {"width_m": 7.0, "depth_m": 6.0, "height_m": 3.4},
        "fridge": {"model": "cryogen_free_dilution_400uW", "position_m": [0.0, 0.0, 0.0],
                   "stages": ["rt", "s50", "s4", "still", "cp", "mxc"], "stage_height_m": fridge_stage_y,
                   "stage_radius_m": {"rt": 0.30, "s50": 0.25, "s4": 0.23, "still": 0.21, "cp": 0.19, "mxc": 0.19},
                   "shields": True, "mag_shield": True, "frame": {"width_m": 1.4, "depth_m": 1.4, "height_m": 2.6}},
        "device": "sc_heavyhex_27",
        "wiring": "Assets/Devices/sc_heavyhex_27/wiring.json",
        "routing": "Assets/Lab/Layouts/sc_lab_standard/routing.json",
        "racks": [
            {"id": "A", "position_m": [2.2, 0.0, -1.5], "height_u": 42,
             "units": ["control_chassis", "mw_generator:4", "digitizer", "iq_mixer_board:8", "dc_source", "power_dist"]},
            {"id": "B", "position_m": [2.9, 0.0, -1.5], "height_u": 42,
             "units": ["vna", "spectrum_analyzer", "oscilloscope", "rt_amplifier:2", "ref_10mhz", "clock_dist", "trigger_unit", "patch_panel", "power_dist"]}],
        "ghs": {"position_m": [-2.5, 0.0, -1.5]},
        "compressor": {"position_m": [-3.2, 0.0, 2.6], "outside": True},
        "props": [{"id": "workstation", "position_m": [2.5, 0.0, 1.8]}, {"id": "bench", "position_m": [-1.5, 0.0, 2.0]}, {"id": "he_dewar", "position_m": [-2.9, 0.0, 0.5], "count": 2}],
        "bookmarks": {
            "Overview": {"pos": [5.0, 3.0, 6.0], "target": [0.0, 1.2, 0.0], "fov_deg": 50},
            "Fridge": {"pos": [1.6, 1.8, 1.6], "target": [0.0, 1.6, 0.0], "fov_deg": 45},
            "MXC": {"pos": [0.45, 1.35, 0.45], "target": [0.0, 1.25, 0.0], "fov_deg": 40},
            "Chip": {"pos": [0.0, 1.16, 0.03], "target": [0.0, 1.13, 0.0], "fov_deg": 30, "scale_island": "chip_micro"},
            "Rack": {"pos": [2.5, 1.5, 0.8], "target": [2.55, 1.2, -1.5], "fov_deg": 45},
            "GHS": {"pos": [-2.5, 1.5, 0.8], "target": [-2.5, 1.0, -1.5], "fov_deg": 45}},
        "layers": ["room", "fridge_exterior", "fridge_interior", "wiring", "rack", "chip", "chip_micro"],
        # ceiling luminaires (spec 17 §11 `lights`): 1.2 × 0.6 m LED panels flush with the ceiling
        "lights": [{"position_m": [x, 3.38, z], "size_m": [1.2, 0.6], "color": [1.0, 0.98, 0.92], "lumens": 4000}
                   for x, z in ((-1.75, -1.5), (1.75, -1.5), (-1.75, 1.5), (1.75, 1.5))],
    }
    write_json("Lab/Layouts/sc_lab_standard/layout.json", "lab.layout", sc)
    stages = ["rt", "s50", "s4", "still", "cp", "mxc"]
    routing = {
        "id": "sc_lab_standard", "layout": "sc_lab_standard",
        "stage_order": stages,
        "feedthrough_ring": {"radius_m": 0.24, "count": 96, "start_angle_deg": 0.0},
        "line_bundles": [  # angular sectors on each plate where line kinds are routed (spec 11 §7 routing graph)
            {"kind": "xy", "sector_deg": [0, 120], "radius_m": {"rt": 0.24, "s50": 0.20, "s4": 0.18, "still": 0.16, "cp": 0.15, "mxc": 0.14}},
            {"kind": "flux", "sector_deg": [120, 180], "radius_m": {"rt": 0.24, "s50": 0.20, "s4": 0.18, "still": 0.16, "cp": 0.15, "mxc": 0.14}},
            {"kind": "readout_in", "sector_deg": [180, 240], "radius_m": {"rt": 0.24, "s50": 0.20, "s4": 0.18, "still": 0.16, "cp": 0.15, "mxc": 0.14}},
            {"kind": "readout_out", "sector_deg": [240, 300], "radius_m": {"rt": 0.24, "s50": 0.20, "s4": 0.18, "still": 0.16, "cp": 0.15, "mxc": 0.14}},
            {"kind": "pump", "sector_deg": [300, 330], "radius_m": {"rt": 0.24, "s50": 0.20, "s4": 0.18, "still": 0.16, "cp": 0.15, "mxc": 0.14}},
            {"kind": "dc", "sector_deg": [330, 360], "radius_m": {"rt": 0.24, "s50": 0.20, "s4": 0.18, "still": 0.16, "cp": 0.15, "mxc": 0.14}}],
        "element_slots": {"attn": {"stage_offset_m": [0.0, -0.04, 0.0]}, "clamp": {"stage_offset_m": [0.0, -0.015, 0.0]},
                          "filter": {"stage_offset_m": [0.0, -0.06, 0.0]}, "iso": {"stage_offset_m": [0.05, -0.05, 0.0]},
                          "amp": {"stage_offset_m": [0.08, -0.06, 0.0]}},
        "segment_lengths_m": {"rt-s50": 0.25, "s50-s4": 0.20, "s4-still": 0.15, "still-cp": 0.10, "cp-mxc": 0.10},
        "rack_to_fridge_cable_m": 6.0,
    }
    write_json("Lab/Layouts/sc_lab_standard/routing.json", "lab.routing", routing)
    ion = {
        "id": "ion_lab_11", "room": {"width_m": 7.0, "depth_m": 6.0, "height_m": 3.4},
        "device": "ion_chain_11", "chamber": {"position_m": [0.0, 1.0, 0.0], "radius_m": 0.12},
        "lasers": [{"id": "laser_path", "wavelength_nm": 369, "role": "cooling_detection"}, {"id": "laser_path", "wavelength_nm": 355, "role": "raman_gates"}, {"id": "laser_path", "wavelength_nm": 935, "role": "repump"}],
        "racks": [{"id": "A", "position_m": [2.2, 0.0, -1.5], "height_u": 42, "units": ["control_chassis", "mw_generator:2", "digitizer", "dc_source", "power_dist"]},
                  {"id": "B", "position_m": [2.9, 0.0, -1.5], "height_u": 42, "units": ["oscilloscope", "ref_10mhz", "clock_dist", "trigger_unit", "patch_panel", "power_dist"]}],
        "props": [{"id": "workstation", "position_m": [2.5, 0.0, 1.8]}, {"id": "optical_table", "position_m": [0.0, 0.0, 0.0], "size_m": [2.4, 1.2]}],
        "bookmarks": {"Overview": {"pos": [5.0, 3.0, 6.0], "target": [0.0, 1.0, 0.0], "fov_deg": 50}, "Trap": {"pos": [0.25, 1.1, 0.25], "target": [0.0, 1.0, 0.0], "fov_deg": 35}, "Rack": {"pos": [2.5, 1.5, 0.8], "target": [2.55, 1.2, -1.5], "fov_deg": 45}},
        "layers": ["room", "chamber", "optics", "rack", "trap_micro"],
        "lights": [{"position_m": [x, 3.38, 0.0], "size_m": [1.2, 0.6], "color": [1.0, 0.98, 0.92], "lumens": 4000} for x in (-2.0, 0.0, 2.0)],
        "detail": "reduced",
    }
    write_json("Lab/Layouts/ion_lab_11/layout.json", "lab.layout", ion)

# ================================================================ QEC codes (spec 16 §1–2, T09 §4–5)
def pauli_commute(a, b):
    """Symplectic commutation of two Pauli strings (same length): True if they commute."""
    anti = 0
    for x, y in zip(a, b):
        if x != "I" and y != "I" and x != y:
            anti ^= 1
    return anti == 0

def code(id, n, k, d, family, stabs, lx, lz, layout, theory, notes=""):
    for i, s in enumerate(stabs):
        assert len(s) == n, (id, i)
        for j, t in enumerate(stabs):
            assert pauli_commute(s, t), f"{id}: generators {i},{j} anticommute"
        for L in lx + lz:
            assert pauli_commute(s, L), f"{id}: logical anticommutes with generator {i}"
    assert len(stabs) == n - k, f"{id}: expected {n-k} generators, got {len(stabs)}"
    for x, z in zip(lx, lz):
        assert not pauli_commute(x, z), f"{id}: logical X and Z must anticommute"
    QEC_CODES.append((id, {"id": id, "n": n, "k": k, "d": d, "family": family, "stabilizers": stabs,
                           "logical_x": lx, "logical_z": lz, "layout": layout, "decoder": "lookup" if n <= 9 else "union_find",
                           "theory": theory, "notes": notes,
                           "convention": "Pauli strings are written left-to-right from qubit 0 (position i is qubit i)."}))

def surface_rotated(d):
    """Rotated planar surface code per spec 16 §2.1 / T09 §5.1. Data at (2i+1, 2j+1); ancillas at even coords."""
    n = d * d
    idx = {(2 * i + 1, 2 * j + 1): i * d + j for i in range(d) for j in range(d)}  # x = column i, y = row j
    data_layout = [[2 * i + 1, 2 * j + 1] for i in range(d) for j in range(d)]
    stabs, ancillas = [], []
    order_x = [(-1, -1), (1, -1), (-1, 1), (1, 1)]   # NW, NE, SW, SE  (x right, y up: N = +y)
    order_x = [(-1, 1), (1, 1), (-1, -1), (1, -1)]
    order_z = [(-1, 1), (-1, -1), (1, 1), (1, -1)]   # NW, SW, NE, SE
    for ax in range(0, 2 * d + 1, 2):
        for ay in range(0, 2 * d + 1, 2):
            i, j = ax // 2, ay // 2
            typ = "X" if (i + j) % 2 == 0 else "Z"
            interior = 0 < ax < 2 * d and 0 < ay < 2 * d
            # boundary half-plaquettes: X checks on top/bottom edges (ay = 0 or 2d), Z on left/right (ax = 0 or 2d)
            on_tb = (ay == 0 or ay == 2 * d) and 0 < ax < 2 * d
            on_lr = (ax == 0 or ax == 2 * d) and 0 < ay < 2 * d
            if not (interior or (typ == "X" and on_tb) or (typ == "Z" and on_lr)):
                continue
            order = order_x if typ == "X" else order_z
            support = [idx[(ax + dx, ay + dy)] for dx, dy in order if (ax + dx, ay + dy) in idx]
            if len(support) not in (2, 4):
                continue
            s = ["I"] * n
            for q in support:
                s[q] = typ
            stabs.append("".join(s))
            ancillas.append({"type": typ, "coord": [ax, ay], "order": support})
    lx = ["".join("X" if (q % d) == 0 and False else ("X" if idx_inv(q, d)[0] == 0 else "I") for q in range(n))]
    lz = ["".join("Z" if idx_inv(q, d)[1] == d - 1 else "I" for q in range(n))]
    return n, stabs, lx, lz, {"data": data_layout, "ancilla": ancillas}

def idx_inv(q, d):
    return q // d, q % d  # (column i, row j)

def qec_codes():
    code("repetition_bitflip_3", 3, 1, 3, "Repetition", ["ZZI", "IZZ"], ["XXX"], ["ZII"],
         {"data": [[0, 0], [1, 0], [2, 0]], "ancilla": [{"type": "Z", "coord": [0.5, 1], "order": [0, 1]}, {"type": "Z", "coord": [1.5, 1], "order": [1, 2]}]},
         [anchor("T09", "4.1")], "Corrects one X error; distance 1 against Z.")
    code("repetition_phaseflip_3", 3, 1, 3, "Repetition", ["XXI", "IXX"], ["XII"], ["ZZZ"],
         {"data": [[0, 0], [1, 0], [2, 0]], "ancilla": [{"type": "X", "coord": [0.5, 1], "order": [0, 1]}, {"type": "X", "coord": [1.5, 1], "order": [1, 2]}]},
         [anchor("T09", "4.2")], "Hadamard-conjugated repetition code; corrects one Z error.")
    shor_stabs = ["ZZIIIIIII", "IZZIIIIII", "IIIZZIIII", "IIIIZZIII", "IIIIIIZZI", "IIIIIIIZZ", "XXXXXXIII", "IIIXXXXXX"]
    code("shor_9", 9, 1, 3, "Shor", shor_stabs, ["ZZZZZZZZZ"], ["XXXXXXXXX"],
         {"data": [[i % 3, i // 3] for i in range(9)], "ancilla": [{"type": "Z", "coord": [0.5 + (i % 2), i // 2 + 0.5], "order": [3 * (i // 2) + (i % 2), 3 * (i // 2) + (i % 2) + 1]} for i in range(6)]
          + [{"type": "X", "coord": [3.5, 0.5], "order": list(range(6))}, {"type": "X", "coord": [3.5, 1.5], "order": list(range(3, 9))}]},
         [anchor("T09", "4.3")], "Logical X and Z are exchanged relative to the usual Shor labelling so that X̄ = Z⊗9 (phase-flip outer code).")
    steane = ["IIIXXXX", "IXXIIXX", "XIXIXIX", "IIIZZZZ", "IZZIIZZ", "ZIZIZIZ"]
    code("steane_7", 7, 1, 3, "Steane", steane, ["XXXXXXX"], ["ZZZZZZZ"],
         {"data": [[0, 0], [1, 0], [2, 0], [0, 1], [1, 1], [2, 1], [1, 2]],
          "ancilla": [{"type": "X", "coord": [3, 0], "order": [3, 4, 5, 6]}, {"type": "X", "coord": [3, 1], "order": [1, 2, 5, 6]}, {"type": "X", "coord": [3, 2], "order": [0, 2, 4, 6]},
                      {"type": "Z", "coord": [-1, 0], "order": [3, 4, 5, 6]}, {"type": "Z", "coord": [-1, 1], "order": [1, 2, 5, 6]}, {"type": "Z", "coord": [-1, 2], "order": [0, 2, 4, 6]}]},
         [anchor("T09", "4.4")], "CSS code from the [7,4] Hamming code; transversal Clifford group.")
    five = ["XZZXI", "IXZZX", "XIXZZ", "ZXIXZ"]
    code("five_qubit", 5, 1, 3, "FiveQubit", five, ["XXXXX"], ["ZZZZZ"],
         {"data": [[0, 0], [1, 0], [2, 0], [3, 0], [4, 0]], "ancilla": [{"type": "M", "coord": [i + 0.5, 1], "order": [i, (i + 1) % 5, (i + 2) % 5, (i + 3) % 5], "pauli": five[i]} for i in range(4)]},
         [anchor("T09", "4.5")], "Smallest perfect code; ancilla type 'M' = mixed Pauli check (needs the 'pauli' field).")
    for d in (3, 5, 7):
        n, stabs, lx, lz, layout = surface_rotated(d)
        code(f"surface_rot_{d}", n, 1, d, "SurfaceRotated", stabs, lx, lz, layout, [anchor("T09", "5.1"), anchor("T09", "5.2")],
             "Rotated planar layout; X checks on top/bottom boundaries, Z checks on left/right; CNOT order X: NW,NE,SW,SE; Z: NW,SW,NE,SE (Tomita–Svore).")

# ================================================================ analysis recipes (spec 22 §6)
def recipe(id, title, theory, program, sweep, shots, extract, fit, results, extra=None):
    """A recipe names the calibration program it sweeps; `program=None` marks a family the runtime
    generates itself (interleaved RB, XEB — see SPEC_DEVIATIONS.md #41), which must then carry a
    `generator` block saying what is generated, and `sweep.input` must name one of the program's
    `input` declarations (tests/Lang/RecipeProgramTest.cpp checks both)."""
    d = {"id": id, "title": title, "theory": theory}
    if program: d["program"] = f"Assets/Programs/Calibration/{program}.qasm"
    d.update({"sweep": sweep, "shots": shots, "extract": extract, "fit": fit, "results": results})
    if extra: d.update(extra)
    RECIPES.append((id, d))

def recipes():
    q = "device.calibration.qubit[q]"
    recipe("t1", "T1 relaxation", anchor("T04", "3.1"), "t1", {"input": "t_delay", "unit": "us", "values": {"log": [0.1, 500, 41]}}, 1000,
           {"channel": "t1.p1", "y": "counts['1'] / shots", "sigma": "binomial"}, {"model": "exp_decay", "report": ["T", "A", "c"]},
           [{"label": "T1", "measured": "fit.T", "model": f"{q}.T1", "tolerance": 0.15}])
    recipe("t2_ramsey", "Ramsey T2*", anchor("T04", "8.1"), "t2_ramsey", {"input": "t_delay", "unit": "us", "values": {"linear": [0.02, 60, 61]}}, 1000,
           {"channel": "ramsey.p1", "y": "counts['1'] / shots", "sigma": "binomial"}, {"model": "ramsey", "report": ["T2", "delta", "phi"]},
           [{"label": "T2*", "measured": "fit.T2", "model": f"{q}.T2_star", "tolerance": 0.2}, {"label": "detuning", "measured": "fit.delta", "model": "input.detuning_MHz", "tolerance": 0.05}],
           {"inputs": {"detuning_MHz": {"unit": "MHz", "default": 0.5}}})
    recipe("t2_echo", "Hahn-echo T2", anchor("T04", "8.2"), "t2_echo", {"input": "t_echo", "unit": "us", "values": {"log": [0.1, 300, 41]}}, 1000,
           {"channel": "echo.p1", "y": "counts['1'] / shots", "sigma": "binomial"}, {"model": "echo", "report": ["T", "n"]},
           [{"label": "T2 echo", "measured": "fit.T", "model": f"{q}.T2", "tolerance": 0.15}])
    recipe("rabi_amp", "Rabi amplitude calibration", anchor("T07", "3"), "rabi_amp", {"input": "amp", "unit": "", "values": {"linear": [0.0, 1.0, 51]}}, 500,
           {"channel": "rabi.p1", "y": "counts['1'] / shots", "sigma": "binomial"}, {"model": "rabi_amp", "report": ["a_pi"]},
           [{"label": "π amplitude", "measured": "fit.a_pi", "model": "device.pulses.x.amp", "tolerance": 0.05}])
    recipe("rabi_time", "Rabi time sweep", anchor("T07", "2"), "rabi_time", {"input": "t_pulse", "unit": "ns", "values": {"linear": [8, 400, 50]}}, 500,
           {"channel": "rabi_t.p1", "y": "counts['1'] / shots", "sigma": "binomial"}, {"model": "rabi_time", "report": ["f", "tau"]},
           [{"label": "Rabi frequency", "measured": "fit.f", "model": "device.pulses.x.rabi_MHz", "tolerance": 0.1}])
    recipe("drag_beta", "DRAG β calibration", anchor("T05", "7"), "drag_beta", {"input": "beta", "unit": "", "values": {"linear": [-2.0, 2.0, 41]}}, 500,
           {"channel": "drag.p1", "y": "counts['1'] / shots", "sigma": "binomial"}, {"model": "linear", "report": ["x0"]},
           [{"label": "β", "measured": "fit.x0", "model": "device.pulses.sx.beta", "tolerance": 0.2}], {"notes": "Sequence (X90·Y180 − Y90·X180) is linear in β around the optimum; report the zero crossing."})
    recipe("qubit_spectroscopy", "Qubit spectroscopy", anchor("T05", "3.2"), "qubit_spectroscopy", {"input": "f_d", "unit": "GHz", "values": {"linear": [4.6, 5.4, 401]}}, 200,
           {"channel": "spec.p1", "y": "counts['1'] / shots", "sigma": "binomial"}, {"model": "lorentzian", "report": ["f0", "fwhm"]},
           [{"label": "f01", "measured": "fit.f0", "model": f"{q}.f01", "tolerance": 0.001}])
    recipe("resonator_spectroscopy", "Resonator spectroscopy (VNA)", anchor("T05", "6.3"), "resonator_spectroscopy", {"input": "f_ro", "unit": "GHz", "values": {"linear": [6.9, 7.3, 1601]}}, 1,
           {"channel": "instr.vna.trace", "y": "s21", "sigma": "trace_noise"}, {"model": "resonator_notch", "report": ["f_r", "Q_i", "Q_c", "Q_l"]},
           [{"label": "f_r", "measured": "fit.f_r", "model": "device.calibration.res[q].f_r", "tolerance": 1e-5}, {"label": "κ/2π", "measured": "2*pi*fit.f_r/fit.Q_l", "model": "device.calibration.res[q].kappa", "tolerance": 0.2}],
           {"instrument": "vna"})
    recipe("punch_out", "Resonator punch-out (power sweep)", anchor("T05", "6.2"), "resonator_spectroscopy", {"input": "f_ro", "unit": "GHz", "values": {"linear": [6.9, 7.3, 801]}, "input2": "power", "unit2": "dBm", "values2": {"linear": [-60, 0, 13]}}, 1,
           {"channel": "instr.vna.trace", "y": "abs(s21)", "sigma": "trace_noise"}, {"model": "none", "report": []},
           [{"label": "dispersive shift 2χ", "measured": "extract.f_low - extract.f_high", "model": "2*device.calibration.res[q].chi", "tolerance": 0.2}], {"instrument": "vna", "heatmap": True})
    recipe("readout_calibration", "Readout calibration (IQ clouds)", anchor("T07", "6"), "readout_calibration", {"input": "prep", "unit": "", "values": [0, 1]}, 5000,
           {"channel": "instr.dig.ch[q].iq", "y": "iq", "sigma": "none"}, {"model": "gmm2", "report": ["mu0", "mu1", "sigma", "F_ro"]},
           [{"label": "assignment fidelity", "measured": "fit.F_ro", "model": f"{q}.readout_fidelity", "tolerance": 0.02}], {"outputs": ["assignment_matrix"]})
    recipe("chevron", "Rabi chevron", anchor("T07", "2"), "rabi_time", {"input": "t_pulse", "unit": "ns", "values": {"linear": [8, 400, 50]}, "input2": "detuning", "unit2": "MHz", "values2": {"linear": [-20, 20, 41]}}, 300,
           {"channel": "chevron.p1", "y": "counts['1'] / shots", "sigma": "binomial"}, {"model": "none", "report": []},
           [{"label": "resonance", "measured": "extract.argmax_detuning", "model": "0", "tolerance": 1.0}], {"heatmap": True})
    recipe("rb_1q", "Single-qubit randomized benchmarking", anchor("T10", "2"), "rb_1q", {"input": "m", "unit": "", "values": [1, 2, 4, 8, 16, 32, 64, 128, 256, 512]}, 200,
           {"channel": "rb.p0", "y": "counts['0'] / shots", "sigma": "binomial", "average_over": "sequences", "sequences": 20}, {"model": "rb_decay", "report": ["p", "r"]},
           [{"label": "error per Clifford", "measured": "fit.r", "model": f"{q}.error_1q * 1.875", "tolerance": 0.3}])
    recipe("rb_2q", "Two-qubit randomized benchmarking", anchor("T10", "2"), "rb_2q", {"input": "m", "unit": "", "values": [1, 2, 4, 8, 16, 32, 64, 128]}, 200,
           {"channel": "rb2.p00", "y": "counts['00'] / shots", "sigma": "binomial", "average_over": "sequences", "sequences": 20}, {"model": "rb_decay", "report": ["p", "r"], "d": 4},
           [{"label": "error per 2q Clifford", "measured": "fit.r", "model": "device.calibration.edge[e].error_2q * 1.5", "tolerance": 0.3}])
    recipe("rb_interleaved", "Interleaved RB", anchor("T10", "2"), None, {"input": "m", "unit": "", "values": [1, 2, 4, 8, 16, 32, 64, 128]}, 200,
           {"channel": "irb.p00", "y": "counts['00'] / shots", "sigma": "binomial", "average_over": "sequences", "sequences": 20}, {"model": "rb_interleaved", "report": ["p_ref", "p_int", "r_gate"], "d": 4},
           [{"label": "cx error", "measured": "fit.r_gate", "model": "device.calibration.edge[e].error_2q", "tolerance": 0.3}],
           {"generator": {"kind": "rb_interleaved", "qubits": 2, "physical_qubits": ["$0", "$1"],
                          "lengths": [1, 2, 4, 8, 16, 32, 64, 128], "sequences": 20, "shots_per_sequence": 200,
                          "families": ["reference", "interleaved"], "interleave": "cx", "inverting": True,
                          "seeded_by": "run seed"},
            "description": "Two families of uniformly random two-qubit Clifford sequences generated by the runtime "
                           "(T10 §2.3): a reference family, each sequence closed by the inverse of its product, and "
                           "the same sequences with cx inserted after every Clifford; r_cx = (1 - p_int/p_ref)·3/4 "
                           "by (2.4). There is no source program: OpenQASM 3 has no random-Clifford construct and "
                           "`pragma qlab.rb` (spec 13 §7) has no slot for an interleaved gate, so a file could only "
                           "spell one fixed sequence — which is not a 2-design and would not decay as A p^m + B "
                           "(SPEC_DEVIATIONS.md #41)."})
    recipe("state_tomography", "State tomography", anchor("T10", "3"), "state_tomography",
           {"input": "basis", "unit": "", "values": [0, 1, 2, 3, 4, 5, 6, 7, 8],
            "labels": ["XX", "XY", "XZ", "YX", "YY", "YZ", "ZX", "ZY", "ZZ"]}, 2000,
           {"channel": "tomo.counts", "y": "counts", "sigma": "multinomial"}, {"model": "mle_state", "report": ["fidelity", "purity"]},
           [{"label": "Bell fidelity", "measured": "fit.fidelity", "model": "1", "tolerance": 0.1}],
           {"description": "The nine two-qubit Pauli settings of T10 §3.1 as basis = 3·b($0) + b($1), b = 0:X 1:Y 2:Z; "
                           "`labels` gives the Pauli string of each index, leftmost letter on $0."})
    recipe("process_tomography", "Process tomography", anchor("T10", "4"), "process_tomography", {"input": "config", "unit": "", "values": {"linear": [0, 143, 144]}}, 1000,
           {"channel": "qpt.counts", "y": "counts", "sigma": "multinomial"}, {"model": "mle_process", "report": ["process_fidelity", "avg_fidelity"]},
           [{"label": "cx average fidelity", "measured": "fit.avg_fidelity", "model": "1 - device.calibration.edge[e].error_2q", "tolerance": 0.05}])
    recipe("zz_ramsey", "ZZ coupling (conditional Ramsey)", anchor("T04", "9.1"), "zz_ramsey", {"input": "t_delay", "unit": "us", "values": {"linear": [0.02, 20, 101]}, "input2": "spectator", "unit2": "", "values2": [0, 1]}, 1000,
           {"channel": "zz.p1", "y": "counts['1'] / shots", "sigma": "binomial"}, {"model": "ramsey", "report": ["delta"]},
           [{"label": "ζ/2π", "measured": "fit.delta[1] - fit.delta[0]", "model": "device.calibration.edge[e].zz", "tolerance": 0.2}],
           {"inputs": {"detuning_MHz": {"unit": "MHz", "default": 0.2}}})
    recipe("xeb", "Cross-entropy benchmarking", anchor("T10", "5"), None, {"input": "depth", "unit": "", "values": [2, 4, 8, 12, 16, 24]}, 500,
           {"channel": "xeb.f", "y": "2**n * mean(p_ideal[x]) - 1", "sigma": "bootstrap", "sequences": 10}, {"model": "rb_decay", "report": ["p"]},
           [{"label": "fidelity per cycle", "measured": "fit.p", "model": "prod(1 - device.calibration.edge[e].error_2q)", "tolerance": 0.3}],
           {"optional": True,
            "generator": {"kind": "xeb", "qubits": 2, "physical_qubits": ["$0", "$1"],
                          "depths": [2, 4, 8, 12, 16, 24], "sequences": 10, "shots_per_sequence": 500,
                          "cycle": {"single_qubit": ["sx", "sy", "sw"], "no_repeat_per_qubit": True,
                                    "entangler": "cz", "pairs": "coupling_map"},
                          "ideal_probabilities": "statevector", "seeded_by": "run seed"},
            "description": "Random circuits generated by the runtime (T10 §5): every cycle gives each qubit a random "
                           "single-qubit gate drawn from {√X, √Y, √W}, never repeating the previous one on that qubit, "
                           "then applies cz on the coupled pair; F_XEB = d·⟨p_U(x)⟩ − 1 by (5.1) decays as p^depth. "
                           "There is no source program: the circuits must differ per sequence and the estimator needs "
                           "the ideal p_U(x) of each one from the state-vector backend, neither of which a fixed "
                           "OpenQASM file can express (SPEC_DEVIATIONS.md #41)."})

# ================================================================ theme (spec 19 §1)
def theme():
    dark = {"bg.base": "#0F1115", "bg.panel": "#161A20", "bg.raised": "#1E232B", "bg.viewport": "#0A0C10", "border": "#2A3039",
            "text.primary": "#E6E9EF", "text.secondary": "#9AA3B2", "text.disabled": "#5F6875", "accent": "#4FA3FF", "accent.soft": "#4FA3FF33",
            "ok": "#3DD68C", "warn": "#F5B841", "err": "#FF5D5D", "sim_only": "#C084FC",
            "class.exact": "#3DD68C", "class.numerical": "#4FA3FF", "class.statistical": "#F5B841", "class.model": "#FF9F43", "class.illustrative": "#9AA3B2"}
    light = {"bg.base": "#F4F5F7", "bg.panel": "#FFFFFF", "bg.raised": "#EEF0F3", "bg.viewport": "#DDE1E6", "border": "#C9CED6",
             "text.primary": "#1A1D22", "text.secondary": "#5B6472", "text.disabled": "#9AA3B2", "accent": "#1F6FEB", "accent.soft": "#1F6FEB22",
             "ok": "#1B8F5A", "warn": "#B7791F", "err": "#C62828", "sim_only": "#7C3AED",
             "class.exact": "#1B8F5A", "class.numerical": "#1F6FEB", "class.statistical": "#B7791F", "class.model": "#C2410C", "class.illustrative": "#5B6472"}
    okabe = ["#E69F00", "#56B4E9", "#009E73", "#F0E442", "#0072B2", "#D55E00", "#CC79A7", "#999999"]
    data = {"palettes": {"dark": dark, "light": light}, "default": "dark",
            "qubit_colors": okabe, "qubit_color_rule": "index i uses qubit_colors[i % 8]; each wrap applies a lightness step of -12 %",
            "phase_colormap": "twilight", "phase_lut_entries": 256, "phase_hue_rule": "H = (phi + pi) / (2 pi); phi = 0 maps to the accent, phi = pi to its complement",
            "colormaps": {"temperature": "inferno", "population": "viridis", "signed": "coolwarm"},
            "radius_px": {"sm": 3, "md": 5, "lg": 8}, "space_px": [4, 8, 12, 16, 24, 32],
            "typography": {"ui": {"font": "Inter", "files": ["Inter-Regular.ttf", "Inter-SemiBold.ttf"], "body_px": 13, "secondary_px": 12, "panel_title_px": 15, "workspace_title_px": 20},
                           "code": {"font": "JetBrains Mono", "files": ["JetBrainsMono-Regular.ttf"], "editor_px": 13, "log_px": 12, "readout_px": 16},
                           "math": {"font": "Latin Modern Math", "file": "LatinModernMath-Regular.otf", "licence": "GUST Font License (Assets/Fonts/GUST-FONT-LICENSE-LatinModernMath.txt)", "note": "the LaTeX typeface; italic and bold letters are the Mathematical Alphanumeric code points of the same face (spec 20 §1)"},
                           "icons": {"set": "Lucide", "sizes_px": [16, 20]}, "oversample": [3, 1], "hinting": False},
            "fidelity_badges": {"Exact": "class.exact", "Numerical": "class.numerical", "Statistical": "class.statistical", "Model": "class.model", "Illustrative": "class.illustrative"}}
    write_json("Lang/theme.json", "ui.theme", data)

# ================================================================ UI strings (spec 19 §3, 15 §9)
def strings():
    data = {
        "app": {"title": "QuantumXLab", "project_untitled": "Untitled project", "physical_lab": "Physical lab",
                "physical_lab_tooltip": "Not observable on hardware — switch off Physical lab mode to use probes."},
        "workspaces": {"lab": "Lab", "program": "Program", "analysis": "Analysis"},
        "panels": {"viewport": "Viewport", "inspector": "Inspector", "component_tree": "Components", "code_editor": "Code Editor", "diagnostics": "Diagnostics",
                   "circuit": "Circuit Diagram", "pulses": "Pulse Schedule", "run_controls": "Run Controls", "results": "Results", "state_views": "State Views",
                   "instruments": "Instruments", "fridge_dashboard": "Fridge Dashboard", "estimates": "Estimates", "theory": "Theory Browser", "examples": "Examples",
                   "project": "Project & Device", "log": "Log", "plots": "Plots", "fits": "Fits", "tomography": "Tomography & Benchmarking", "qec": "Error Correction", "tour": "Guided Tour"},
        "menu": {"file": "File", "new": "New project", "open": "Open…", "save": "Save", "save_as": "Save as…", "export": "Export", "export_qasm": "Compiled OpenQASM 3…",
                 "export_results": "Results (JSON/CSV)…", "export_screenshot": "Screenshot (PNG)…", "export_report": "Run report (HTML)…", "export_state": "State vector (.npy, Simulator-only)…",
                 "edit": "Edit", "undo": "Undo", "redo": "Redo", "view": "View", "run": "Run", "compile": "Compile", "run_program": "Run", "stop": "Stop", "step_gate": "Step gate", "step_shot": "Step shot",
                 "device": "Device", "help": "Help", "theory": "Theory", "about": "About"},
        "run": {"device": "Device", "backend": "Backend", "backend_auto": "auto", "shots": "Shots", "seed": "Seed", "snapshot_cadence": "Snapshot every", "status_idle": "Idle",
                "status_compiling": "Compiling…", "status_running": "Running shot {shot} / {shots}", "status_done": "Done in {ms} ms", "noise_gates_only": "Noise: gates only (unscheduled)",
                "noise_full": "Noise: gates + idles", "noise_off": "Noise off"},
        "fidelity_class": {"Exact": "Exact", "Numerical": "Numerical", "Statistical": "Statistical", "Model": "Model", "Illustrative": "Illustrative",
                           "tooltip": {"Exact": "Closed-form or exact linear algebra on the full state; error at machine precision.",
                                       "Numerical": "Validated numerical solution of a differential equation with a stated error bound.",
                                       "Statistical": "Sampled quantity; the interval shown is the 68.3 % Wilson interval.",
                                       "Model": "Phenomenological or calibration-derived; correct in form, parameters from typical published ranges.",
                                       "Illustrative": "Visual aid conveying a mechanism qualitatively; not a computed observable."}},
        "sim_only": {"badge": "Simulator-only", "tooltip": "This quantity cannot be observed on a physical machine. It is computed from the simulator's state."},
        "estimate_assumptions": {
            "queue_time_excluded": "Wall time excludes any scheduler or queue wait on a shared machine.",
            "reset_policy:active": "Qubits are reset by measurement followed by a conditional X (active reset); duration from device.json control.reset.",
            "reset_policy:passive": "Qubits relax for 5·T1 (or the device repetition delay, whichever is longer) between shots.",
            "independent_errors": "Gate, idle and readout errors are treated as statistically independent and multiplied.",
            "average_gate_fidelities": "Each gate error is the average (Clifford-twirled) error rate from randomized benchmarking; coherent errors are not tracked separately.",
            "no_crosstalk": "Classical drive crosstalk and ZZ during idles are neglected unless present in the calibration file.",
            "calibration_static": "Calibration values are constant over the run (no drift, no recalibration).",
            "readout_from_calibration": "Readout duration and assignment fidelity are taken from the device calibration.",
            "feedforward_latency": "Each classical branch adds the device feedback latency to the critical path.",
            "idle_thermal_relaxation": "Idle error per interval is the thermal-relaxation infidelity [2(1−e^{−t/T2}) + (1−e^{−t/T1})]/6.",
            "surface_code_scaling": "Fault-tolerant estimates use p_L ≈ 0.1 (p/0.01)^((d+1)/2) with a rotated surface code and a 1.5× routing factor.",
            "host_benchmark": "Classical cost uses the per-amplitude gate time measured on this machine at startup."},
        "estimates": {"wall_time": "Wall time on hardware", "fidelity_fast": "Fidelity (product model)", "fidelity_simulated": "Fidelity (noisy simulation)",
                      "resources": "Resources", "classical_cost": "Classical simulation cost", "qec": "If run fault-tolerantly", "comparison": "Other devices", "assumptions": "Assumptions"},
        "inspector": {"function": "Function", "physics": "Physics", "spec_sheet": "Specification", "live": "Live values", "theory": "Theory", "children": "Contains",
                      "no_selection": "Select a component in the viewport or the component tree."},
        "instruments": {"acquire": "Acquire", "live": "Live", "stop": "Stop", "single": "Single", "autoscale": "Autoscale", "export": "Export trace"},
        "fridge": {"cooldown": "Start cooldown", "warmup": "Warm up", "state": "State", "margin": "Cooling margin", "flow": "3He flow", "still_power": "Still heater", "mxc_power": "MXC heater"},
        "diagnostics": {"errors": "{n} errors", "warnings": "{n} warnings", "none": "No diagnostics", "goto": "Go to source"},
        "keys": {"run": "F5", "compile": "F6", "step_gate": "F10", "step_shot": "F11", "workspace_lab": "Ctrl+1", "workspace_program": "Ctrl+2", "workspace_analysis": "Ctrl+3", "physical_lab": "Ctrl+L"},
    }
    write_json("Lang/strings.en.json", "ui.strings", data)

# ================================================================ write everything
def write_all():
    layouts(); qec_codes(); recipes(); theme(); strings()
    write_json("Theory/equations.json", "theory.equations", {"equations": EQUATIONS})
    write_json("Theory/assumptions.json", "theory.assumptions", {"assumptions": ASSUMPTIONS})
    write_json("Theory/gates.json", "theory.gates", {"gates": GATES, "convention": "little-endian; first listed qubit is the least significant bit; matrices in the |q_b q_a> basis for g q[a], q[b]"})
    for c in COMPONENTS:
        write_json(f"Lab/Components/{c['id']}/component.json", "lab.component", c)
    for cid, c in QEC_CODES:
        write_json(f"QEC/{cid}.json", "qec.code", c)
    for rid, r in RECIPES:
        write_json(f"Analysis/{rid}.json", "analysis.recipe", r)

# ================================================================ validation
def validate():
    problems = list(ERRORS)
    def load(rel):
        with open(os.path.join(ASSETS, rel), encoding="utf8") as f:
            j = json.load(f)
        for k in ("kind", "schema", "app", "created"):
            if k not in j.get("qxl", {}): problems.append(f"{rel}: envelope missing {k}")
        return j["data"]
    eqs = load("Theory/equations.json")["equations"]; eq_ids = [e["id"] for e in eqs]
    if len(eq_ids) != len(set(eq_ids)): problems.append("duplicate equation ids")
    assum = load("Theory/assumptions.json")["assumptions"]
    def check_anchor(a, where):
        m = re.match(r"^(T\d\d)#(.+)$", a)
        if not m or m.group(2) not in ANCHORS.get(m.group(1), ()): problems.append(f"{where}: unresolved anchor {a}")
    for e in eqs:
        for t in e["terms"]:
            sym = t["symbol"].split(",")[0].strip()
            if sym not in e["latex"]: problems.append(f"eq {e['id']}: term symbol {sym!r} not in latex")
        for a in e["assumptions"]:
            if a not in assum: problems.append(f"eq {e['id']}: unknown assumption {a}")
        check_anchor(e["theory"], f"eq {e['id']}")
        if not e["plain"]: problems.append(f"eq {e['id']}: missing plain")
    gates = load("Theory/gates.json")["gates"]
    if len({g["name"] for g in gates}) != len(gates): problems.append("duplicate gate names")
    comp_ids = set()
    for d in sorted(os.listdir(os.path.join(ASSETS, "Lab/Components"))):
        c = load(f"Lab/Components/{d}/component.json")
        if c["id"] != d: problems.append(f"component dir {d} != id {c['id']}")
        comp_ids.add(c["id"])
        n = len(c["function"].split())
        if not 40 <= n <= 200: problems.append(f"{d}: function has {n} words")
        for e in c["physics"]["equations"]:
            if e not in eq_ids: problems.append(f"{d}: equation {e} unknown")
        if not c["spec_sheet"]: problems.append(f"{d}: empty spec sheet")
        for r in c["spec_sheet"]:
            if "binding" in r and "class" not in r: problems.append(f"{d}: live row {r['field']} lacks class")
            if "unit" not in r: problems.append(f"{d}: row {r['field']} lacks unit")
        if c["category"] not in ("structure", "infra") and not c["theory"]: problems.append(f"{d}: no theory anchor")
        for a in c["theory"]: check_anchor(a, d)
        if len(c["tooltip"]) > 90: problems.append(f"{d}: tooltip > 90 chars")
        for k in ("geometry", "lod"):
            if k not in c: problems.append(f"{d}: missing {k}")
    spec17 = ["fridge_frame","gantry_hoist","top_plate_300K","feedthrough_sma","ovc","stage_50K","shield_50K","stage_4K","shield_4K","stage_still","shield_still","stage_cp","stage_mxc",
              "pulse_tube_head","pt_flex_lines","pt_remote_motor","condensing_line","flow_impedance","hx_continuous","hx_step","still_heater","mxc_heater","thermometer_ruo2","thermometer_cernox",
              "mag_shield_mumetal","mag_shield_al","cold_finger","sample_puck","chip_package","pcb","coax_segment","attenuator","ir_filter","lpf","thermal_clamp","dc_loom","isolator","circulator",
              "jpa","twpa","pump_line","hemt","directional_coupler","bias_tee","rt_amplifier","cable_tray","mw_generator","control_chassis","digitizer","iq_mixer_board","dc_source","vna",
              "spectrum_analyzer","oscilloscope","ref_10mhz","clock_dist","trigger_unit","power_dist","patch_panel","ghs_cabinet","turbo_pump","scroll_pump","he3_compressor","ln2_trap",
              "pressure_gauge","flow_meter","valve","dump_tank","substrate","ground_plane","transmon_pad","junction","squid_loop","readout_resonator","feedline","purcell_filter","coupler_fixed",
              "coupler_tunable","drive_line","flux_line","airbridge","bond_pad","flip_chip_bump","package_cavity","vacuum_chamber","trap_chip","laser_path","imaging_objective","pmt_camera",
              "helical_resonator","magnetic_coils"]
    for i in spec17:
        if i not in comp_ids: problems.append(f"spec 17 §3 id missing: {i}")
    for lay in ("sc_lab_standard", "ion_lab_11"):
        L = load(f"Lab/Layouts/{lay}/layout.json")
        for r in L["racks"]:
            for u in r["units"]:
                if u.split(":")[0] not in comp_ids: problems.append(f"layout {lay}: rack unit {u} unknown")
    for fn in sorted(os.listdir(os.path.join(ASSETS, "QEC"))):
        c = load(f"QEC/{fn}")
        if len(c["stabilizers"]) != c["n"] - c["k"]: problems.append(f"{fn}: generator count")
        for a in c["theory"]: check_anchor(a, fn)
    # exhaustive distance check for the smallest surface code (n = 9): min weight of a logical operator
    c = load("QEC/surface_rot_3.json")
    dist = min_distance(c["stabilizers"], c["logical_x"] + c["logical_z"], c["n"])
    if dist != c["d"]: problems.append(f"surface_rot_3: computed distance {dist} != {c['d']}")
    for fn in sorted(os.listdir(os.path.join(ASSETS, "Analysis"))):
        r = load(f"Analysis/{fn}")
        check_anchor(r["theory"], fn)
        if "program" not in r:
            # A recipe with no program is a family the runtime generates (SPEC_DEVIATIONS.md #41);
            # it must say what it generates, or it is the gap this check exists to catch.
            if not r.get("generator", {}).get("kind"):
                problems.append(f"{fn}: no program and no generator.kind")
            continue
        if not os.path.exists(os.path.join(ROOT, r["program"])):
            problems.append(f"{fn}: program {r['program']} does not exist")
            continue
        # spec 22 §6: the sweep drives the program's `input` variables, so every swept name must be
        # one of them (declared identifiers, not keywords: `duration` and `delay` are reserved).
        src = open(os.path.join(ROOT, r["program"]), encoding="utf8").read()
        inputs = set(re.findall(r"^input\s+\w+(?:\[\d+\])?\s+(\w+)", src, re.M))
        rb = re.search(r"^pragma qlab\.rb\s+(.*)$", src, re.M)
        if rb:
            # `pragma qlab.rb <n_qubits> <lengths...> <samples>` (spec 13 §7) generates the family,
            # so the sweep variable is its sequence length: check the lengths and the sample count.
            args = [int(x) for x in rb.group(1).split()]
            lengths, samples = args[1:-1], args[-1]
            for v in r["sweep"]["values"]:
                if v not in lengths: problems.append(f"{fn}: sweep value {v} is not an rb length of {r['program']}")
            seqs = r.get("extract", {}).get("sequences")
            if seqs and seqs != samples: problems.append(f"{fn}: {seqs} sequences but {r['program']} generates {samples}")
        else:
            for key in ("input", "input2"):
                name = r.get("sweep", {}).get(key)
                if name and name not in inputs:
                    problems.append(f"{fn}: sweep {key} '{name}' is not an input of {r['program']}")
        for name in r.get("inputs", {}):
            if name not in inputs:
                problems.append(f"{fn}: inputs '{name}' is not an input of {r['program']}")
    load("Lang/theme.json"); load("Lang/strings.en.json")
    return problems

def min_distance(stabs, logicals, n):
    """Minimum weight of a Pauli that commutes with all stabilizers and is not in the stabilizer group (n ≤ 9)."""
    import itertools
    P = "IXYZ"
    def commutes_all(p): return all(pauli_commute(p, s) for s in stabs)
    def anticommutes_some_logical(p): return any(not pauli_commute(p, L) for L in logicals)
    best = n
    for w in range(1, n + 1):
        if w >= best: break
        for pos in itertools.combinations(range(n), w):
            for letters in itertools.product("XYZ", repeat=w):
                p = ["I"] * n
                for q, l in zip(pos, letters): p[q] = l
                p = "".join(p)
                if commutes_all(p) and anticommutes_some_logical(p):
                    return w
    return best

if __name__ == "__main__":
    write_all()
    probs = validate()
    for p in probs: print("PROBLEM:", p)
    print(f"equations={len(EQUATIONS)} assumptions={len(ASSUMPTIONS)} gates={len(GATES)} components={len(COMPONENTS)} codes={len(QEC_CODES)} recipes={len(RECIPES)}")
    sys.exit(1 if probs else 0)
