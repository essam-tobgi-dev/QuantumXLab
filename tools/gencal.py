#!/usr/bin/env python3
"""gencal — generates the shipped device assets of QuantumXLab (spec 09 §8).

Produces, for every device id, Assets/Devices/<id>/{device.json, calibration.json,
pulses.json, wiring.json} wrapped in the core JSON envelope (spec 04 §8).
Standard library only. Deterministic for a given seed.

    python3 tools/gencal.py [--seed N] [--out Assets/Devices] [--device ID]
"""
import argparse, json, math, os, random, sys, datetime

APP_VERSION = "0.1.0"
DEVICE_IDS = ["sc_fixed_5", "sc_heavyhex_27", "sc_heavyhex_127", "sc_tunable_grid_54",
              "ion_chain_11", "ion_chain_32"]

# ----------------------------------------------------------------------------- envelope
def envelope(kind, data, created=None):
    return {"qxl": {"kind": kind, "schema": 1, "app": APP_VERSION,
                    "created": created or "2026-09-17T00:00:00Z"},
            "data": data}

def write_json(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf8") as f:
        json.dump(obj, f, indent=1, sort_keys=False)
        f.write("\n")

def triple(value, sigma, source):
    return [round_sig(value), round_sig(sigma), source]

def round_sig(x, sig=7):
    if x == 0 or not math.isfinite(x):
        return x
    if isinstance(x, int):
        return x
    return float(f"{x:.{sig}g}")

# ----------------------------------------------------------------------------- topologies
def topo_t5():
    """sc_fixed_5: T shape 0-1-2, 1-3, 3-4 (spec 09 §4.1)."""
    pos = {0: (0, 0), 1: (1, 0), 2: (2, 0), 3: (1, 1), 4: (1, 2)}
    edges = [(0, 1), (1, 2), (1, 3), (3, 4)]
    return [{"index": i, "kind": "data", "pos": [float(pos[i][0]), float(pos[i][1])]} for i in range(5)], edges

def topo_heavyhex_27():
    """27-qubit heavy-hex (spec 09 §4.2): 28 edges, degree 1/2/3 pattern."""
    pos = {0: (0, 0), 1: (1, 0), 2: (2, 0), 3: (3, 0), 4: (1, 1), 5: (3, 1),
           6: (0, 2), 7: (1, 2), 8: (3, 2), 9: (4, 2), 10: (1, 3), 11: (3, 3),
           12: (1, 4), 13: (2, 4), 14: (3, 4), 15: (1, 5), 16: (3, 5),
           17: (0, 6), 18: (1, 6), 19: (3, 6), 20: (4, 6), 21: (1, 7), 22: (3, 7),
           23: (1, 8), 24: (2, 8), 25: (3, 8), 26: (4, 8)}
    edges = [(0, 1), (1, 2), (1, 4), (2, 3), (3, 5), (4, 7), (5, 8), (6, 7), (7, 10), (8, 9),
             (8, 11), (10, 12), (11, 14), (12, 13), (12, 15), (13, 14), (14, 16), (15, 18),
             (16, 19), (17, 18), (18, 21), (19, 20), (19, 22), (21, 23), (22, 25), (23, 24),
             (24, 25), (25, 26)]
    assert len(edges) == 28
    return [{"index": i, "kind": "data", "pos": [float(pos[i][0]), float(pos[i][1])]} for i in range(27)], edges

def topo_heavyhex_127():
    """127-qubit heavy-hex: rows of 14,15,15,15,15,15,14 qubits plus 4 bridge qubits per gap.
    Row edges 96 + bridge edges 48 = 144 (spec 09 §4.3)."""
    qubits, edges = [], []
    idx = 0
    rows_x = [list(range(0, 14))] + [list(range(0, 15))] * 5 + [list(range(1, 15))]
    row_ids = []
    bridge_cols = [[0, 4, 8, 12], [2, 6, 10, 14]]
    for r, xs in enumerate(rows_x):
        ids = {}
        for x in xs:
            ids[x] = idx
            qubits.append({"index": idx, "kind": "data", "pos": [float(x), float(2 * r)]})
            idx += 1
        for x in xs[:-1]:
            edges.append((ids[x], ids[x + 1]))
        row_ids.append(ids)
        if r < len(rows_x) - 1:
            for x in bridge_cols[r % 2]:
                qubits.append({"index": idx, "kind": "data", "pos": [float(x), float(2 * r + 1)]})
                edges.append((ids[x], idx))
                row_ids.append(("bridge", idx, x))
                idx += 1
    # connect bridges to the next row
    # rebuild: bridges were appended before the next row existed; connect now
    bridges = [q for q in qubits if (q["pos"][1] % 2) == 1]
    row_map = {}
    for q in qubits:
        if q["pos"][1] % 2 == 0:
            row_map[(q["pos"][0], q["pos"][1])] = q["index"]
    for b in bridges:
        x, y = b["pos"]
        below = row_map.get((x, y + 1))
        assert below is not None
        edges.append((b["index"], below))
    assert len(qubits) == 127, len(qubits)
    assert len(edges) == 144, len(edges)
    return qubits, edges

def topo_grid_54():
    """6x9 square grid of data qubits with one tunable coupler qubit per edge (spec 09 §4.4)."""
    rows, cols = 6, 9
    qubits, edges, couplers = [], [], []
    idx = 0
    id_of = {}
    for r in range(rows):
        for c in range(cols):
            id_of[(r, c)] = idx
            qubits.append({"index": idx, "kind": "data", "pos": [float(c), float(r)]})
            idx += 1
    for r in range(rows):
        for c in range(cols):
            for (dr, dc) in ((0, 1), (1, 0)):
                r2, c2 = r + dr, c + dc
                if r2 < rows and c2 < cols:
                    cq = idx
                    qubits.append({"index": cq, "kind": "coupler",
                                   "pos": [(c + c2) / 2.0, (r + r2) / 2.0]})
                    idx += 1
                    edges.append((id_of[(r, c)], id_of[(r2, c2)], cq))
    return qubits, edges

def topo_chain(n):
    return [{"index": i, "kind": "data", "pos": [float(i), 0.0]} for i in range(n)], []

# ----------------------------------------------------------------------------- frequency plan
COLLISION = {  # spec 09 §6 thresholds (GHz)
    "degenerate": 0.017, "straddle": 0.004, "two_photon": 0.030,
    "cr_max": 0.200, "cr_min": 0.030, "spectator": 0.017,
}

def neighbours(n, edges):
    nb = {i: set() for i in range(n)}
    for e in edges:
        a, b = e[0], e[1]
        nb[a].add(b); nb[b].add(a)
    return nb

def collisions(f, alpha, edges, directed_cr, nb):
    """Return list of (rule, i, j) violations for frequencies f (GHz) and anharmonicities alpha (GHz)."""
    out = []
    pairs = set()
    for e in edges:
        pairs.add((e[0], e[1]))
    # next-nearest pairs (sharing a neighbour)
    nn = set()
    for c in nb:
        ns = sorted(nb[c])
        for i in range(len(ns)):
            for j in range(i + 1, len(ns)):
                nn.add((ns[i], ns[j]))
    for (i, j) in pairs | nn:
        if abs(f[i] - f[j]) < COLLISION["degenerate"]:
            out.append(("degenerate", i, j))
        if abs(f[i] + alpha[i] / 2 - f[j]) < COLLISION["straddle"] or abs(f[j] + alpha[j] / 2 - f[i]) < COLLISION["straddle"]:
            out.append(("straddle", i, j))
        if abs(2 * f[i] + alpha[i] - 2 * f[j]) < COLLISION["two_photon"] or abs(2 * f[j] + alpha[j] - 2 * f[i]) < COLLISION["two_photon"]:
            out.append(("two_photon", i, j))
    if directed_cr:
        for (c, t) in pairs:
            d = abs(f[c] - f[t])
            if d > COLLISION["cr_max"] + 1e-9:
                out.append(("cr_too_slow", c, t))
            if d < COLLISION["cr_min"]:
                out.append(("cr_unstable", c, t))
            for s in nb[c]:
                if s != t and abs(f[t] - f[s]) < COLLISION["spectator"]:
                    out.append(("spectator", t, s))
    return out

def path_labels(n, edges, nb, groups):
    """Five-group heavy-hex plan (spec 09 §4.2): bipartition A/B; A qubits take groups 1 or 3,
    B qubits take 2, or 0 when every A neighbour is 1, or 4 when every A neighbour is 3.
    Every edge then differs by exactly one group (120 MHz); spectator/degenerate conflicts
    between B qubits sharing an A neighbour are resolved by the ±20 MHz repair loop."""
    side = [None] * n
    for root in range(n):
        if side[root] is not None:
            continue
        side[root] = 0
        stack = [root]
        while stack:
            u = stack.pop()
            for v in nb[u]:
                if side[v] is None:
                    side[v] = 1 - side[u]
                    stack.append(v)
    label = [None] * n
    a_count = 0
    for i in range(n):
        if side[i] == 0:
            label[i] = 1 if (a_count % 2 == 0) else 3
            a_count += 1
    for i in range(n):
        if side[i] == 1:
            ls = {label[j] for j in nb[i]}
            if ls == {1}:
                label[i] = 0
            elif ls == {3}:
                label[i] = 4
            else:
                label[i] = 2
    return label

def assign_frequencies(dev, rng):
    """Returns (f01 list GHz, alpha list GHz) for transmon devices per spec 09 §4 / §8."""
    n = len(dev["qubits"]); tech = dev["technology"]
    edges = [(e["a"], e["b"], e.get("coupler")) if isinstance(e, dict) else e for e in dev["edges"]]
    nb = neighbours(n, edges)
    alpha_nom = dev["_nominal"]["alpha_ghz"]
    alpha = [alpha_nom + rng.gauss(0, 0.006) for _ in range(n)]
    if dev["id"] == "sc_fixed_5":
        f = [4.8, 5.0, 5.2, 4.9, 5.1]
    elif tech == "transmon_fixed":
        groups = [4.80, 4.92, 5.04, 5.16, 5.28]
        lab = path_labels(n, edges, nb, len(groups))
        f = [groups[lab[i]] + rng.gauss(0, 0.015) for i in range(n)]
    else:  # tunable / tunable coupler grid: staggered idle frequencies 6.0–6.8 GHz
        f = []
        for q in dev["qubits"]:
            if q["kind"] == "coupler":
                f.append(dev["_nominal"]["coupler_idle_ghz"] + rng.gauss(0, 0.05))
            else:
                r, c = int(round(q["pos"][1])), int(round(q["pos"][0]))
                f.append(6.0 + 0.2 * ((r + 2 * c) % 4) + rng.gauss(0, 0.015))
        for i, q in enumerate(dev["qubits"]):
            if q["kind"] == "coupler":
                alpha[i] = dev["_nominal"]["coupler_alpha_ghz"]
    directed = tech == "transmon_fixed"
    data_edges = [(e[0], e[1]) for e in edges]
    # collision repair (spec 09 §8): for a qubit in the first violation, try the candidate
    # frequencies planned ± k·20 MHz (k ≤ 6) and keep the one that minimises the global
    # violation count; iterate until the plan passes.
    base = list(f)
    for _ in range(3000):
        viol = collisions(f, alpha, data_edges, directed, nb)
        if not viol:
            break
        viol.sort()
        rule, i, j = viol[0]
        best = (len(viol), None, None)
        for victim in (i, j):
            for k in range(0, 7):
                for sign in (1, -1):
                    cand = base[victim] + sign * 0.020 * k
                    old_f = f[victim]
                    f[victim] = cand
                    cnt = len(collisions(f, alpha, data_edges, directed, nb))
                    f[victim] = old_f
                    if cnt < best[0]:
                        best = (cnt, victim, cand)
        if best[1] is None:
            # no single move helps: perturb the pair randomly within ±60 MHz and continue
            victim = i if rng.random() < 0.5 else j
            f[victim] = base[victim] + rng.uniform(-0.06, 0.06)
        else:
            f[best[1]] = best[2]
    viol = collisions(f, alpha, data_edges, directed, nb)
    if viol:
        raise SystemExit(f"{dev['id']}: unresolved frequency collisions: {viol[:5]}")
    return f, alpha

# ----------------------------------------------------------------------------- nominals (spec 09 §4)
TRANSMON_FIXED_NOMINAL = {
    "alpha_ghz": -0.330, "t1_us": 110.0, "t2_echo_us": 95.0, "t2_star_us": 55.0,
    "sx_ns": 32, "sx_err": 3.5e-4, "cx_ns": 440, "cx_err": 9.0e-3, "zz_khz": -60.0,
    "ro_ns": 700, "ro_f": 0.975, "thermal_pop": 0.015, "dt_ps": 222, "g_mhz": 3.2,
    "readout_kappa_mhz": 3.9, "readout_chi_mhz": -0.62, "leak_1q": 1.5e-5, "leak_2q": 1.5e-4,
    "reset_error": 0.004,
}
NOMINALS = {
    "sc_fixed_5": dict(TRANSMON_FIXED_NOMINAL),
    "sc_heavyhex_27": dict(TRANSMON_FIXED_NOMINAL, t1_us=150.0, cx_ns=380, cx_err=7.5e-3, ro_ns=640, ro_f=0.98),
    "sc_heavyhex_127": dict(TRANSMON_FIXED_NOMINAL, t1_us=150.0, cx_ns=380, cx_err=7.5e-3, ro_ns=640, ro_f=0.98, t1_sigma_ln=0.35),
    "sc_tunable_grid_54": {
        "alpha_ghz": -0.210, "t1_us": 25.0, "t2_echo_us": 30.0, "t2_star_us": 18.0,
        "sx_ns": 25, "sx_err": 6e-4, "cz_ns": 40, "cz_err": 5e-3, "siswap_ns": 32, "siswap_err": 6e-3,
        "zz_khz": 5.0, "ro_ns": 500, "ro_f": 0.97, "thermal_pop": 0.02, "dt_ps": 222,
        "coupler_idle_ghz": 8.5, "coupler_alpha_ghz": -0.100, "g_eff_max_mhz": 40.0, "g_mhz": 40.0,
        "readout_kappa_mhz": 5.0, "readout_chi_mhz": -0.9, "leak_1q": 3e-5, "leak_2q": 5e-4,
        "reset_error": 0.006,
    },
    "ion_chain_11": {"omega_z_mhz": 0.30, "omega_r_mhz": 3.0, "t2_star_us": 1.0e6, "t2_echo_us": 2.5e6,
                     "sx_us": 10.0, "sx_err": 1e-4, "ms_us": 200.0, "ms_err": 4e-3, "ro_us": 300.0,
                     "ro_f": 0.997, "heating": 50.0, "eta": 0.08, "f_qubit_ghz": 12.642812118},
    "ion_chain_32": {"omega_z_mhz": 0.18, "omega_r_mhz": 3.0, "t2_star_us": 1.0e6, "t2_echo_us": 2.5e6,
                     "sx_us": 10.0, "sx_err": 1e-4, "ms_us": 250.0, "ms_err": 6e-3, "ro_us": 300.0,
                     "ro_f": 0.997, "heating": 50.0, "eta": 0.08, "f_qubit_ghz": 12.642812118},
}

def feedlines_for(n_data_qubits, per_line):
    lines, cur = [], []
    for q in range(n_data_qubits):
        cur.append(q)
        if len(cur) == per_line:
            lines.append(cur); cur = []
    if cur:
        lines.append(cur)
    return [{"id": i, "qubits": qs} for i, qs in enumerate(lines)]

def build_device(dev_id, rng):
    nom = NOMINALS[dev_id]
    if dev_id == "sc_fixed_5":
        qubits, edges = topo_t5(); tech = "transmon_fixed"; name = "5-qubit T-shaped fixed-frequency transmon processor"
    elif dev_id == "sc_heavyhex_27":
        qubits, edges = topo_heavyhex_27(); tech = "transmon_fixed"; name = "27-qubit heavy-hex transmon processor"
    elif dev_id == "sc_heavyhex_127":
        qubits, edges = topo_heavyhex_127(); tech = "transmon_fixed"; name = "127-qubit heavy-hex transmon processor"
    elif dev_id == "sc_tunable_grid_54":
        qubits, edges = topo_grid_54(); tech = "transmon_tunable_coupler"; name = "54-qubit square-grid tunable-coupler transmon processor"
    elif dev_id.startswith("ion_chain"):
        n = int(dev_id.split("_")[-1]); qubits, edges = topo_chain(n); tech = "ion_chain"
        name = f"{n}-ion 171Yb+ hyperfine chain"
    else:
        raise SystemExit("unknown device " + dev_id)
    d = {"id": dev_id, "display_name": name, "technology": tech,
         "qudit_dimension": 2 if tech == "ion_chain" else 3, "qubits": qubits}
    if tech == "ion_chain":
        d["edges"] = [{"a": "*", "b": "*", "kind": "all_to_all", "directed": False}]
        d["native_gates"] = {"single": ["rx", "ry", "rz"], "two": ["ms"], "measure": "fluorescence", "reset": "optical_pumping"}
        d["timing"] = {"dt_ps": 1000, "granularity_samples": 16, "min_pulse_samples": 64,
                       "readout_ns": int(nom["ro_us"] * 1000), "readout_ringdown_ns": 0, "repetition_delay_us": 0}
        d["control"] = {"load_time_ms": 100, "rep_overhead_us": 100.0, "feedback_latency_ns": 20000,
                        "reset": {"policy": "cooling", "cooling_time_ms": 1.5}, "max_program_duration_ms": 5000}
        d["qec"] = {"cycle_time_us": 5000.0}
        d["readout"] = {"method": "state_dependent_fluorescence", "detection_window_us": nom["ro_us"],
                        "collection_efficiency": 0.01, "bright_rate_per_us": 0.05, "dark_rate_per_us": 0.0005}
        d["motional_modes"] = {"axis": "axial" if len(qubits) <= 11 else "radial", "cutoff": 8,
                               "heating_quanta_per_s": nom["heating"],
                               "omega_z_mhz": nom["omega_z_mhz"], "omega_r_mhz": nom["omega_r_mhz"]}
        d["ion"] = {"species": "171Yb+", "qubit": "hyperfine_clock", "f_qubit_ghz": nom["f_qubit_ghz"],
                    "raman_wavelength_nm": 355.0, "lamb_dicke_nominal": nom["eta"]}
    else:
        if tech == "transmon_fixed":
            d["edges"] = [{"a": a, "b": b, "kind": "fixed_capacitive", "directed": True} for (a, b) in edges]
            d["native_gates"] = {"single": ["x", "sx", "rz", "id"], "two": ["cx", "ecr"],
                                 "measure": "dispersive", "reset": "measure_conditional_x"}
        else:
            d["edges"] = [{"a": a, "b": b, "kind": "tunable_coupler", "coupler": c, "directed": False} for (a, b, c) in edges]
            d["native_gates"] = {"single": ["x", "sx", "rz", "id"], "two": ["cz", "siswap"],
                                 "measure": "dispersive", "reset": "measure_conditional_x"}
        d["timing"] = {"dt_ps": nom["dt_ps"], "granularity_samples": 16, "min_pulse_samples": 64,
                       "readout_ns": nom["ro_ns"], "readout_ringdown_ns": 160, "repetition_delay_us": 250,
                       "feedforward_ns": 200}
        d["control"] = {"load_time_ms": 50, "rep_overhead_us": 1.0, "feedback_latency_ns": 200,
                        "reset": {"policy": "active", "active_duration_ns": None, "passive_multiplier": 5},
                        "max_program_duration_ms": 10}
        d["qec"] = {"cycle_time_us": 0.8 if tech == "transmon_tunable_coupler" else 1.0}
        n_data = sum(1 for q in qubits if q["kind"] == "data")
        per_line = 7 if dev_id == "sc_heavyhex_27" else (8 if dev_id == "sc_heavyhex_127" else (9 if dev_id == "sc_tunable_grid_54" else 5))
        fls = feedlines_for(n_data, per_line)
        res_f = []
        for fl in fls:
            for k, q in enumerate(fl["qubits"]):
                res_f.append(round(7.00 + 0.045 * k + max(-0.002, min(0.002, rng.gauss(0, 0.001))), 5))
        d["readout"] = {"resonator_f_ghz": res_f, "feedlines": fls, "purcell_filter": True,
                        "resonator_spacing_mhz_min": 40}
        d["frequency_plan"] = ({"band_ghz": [4.6, 5.4], "min_neighbour_detuning_mhz": 50}
                               if tech == "transmon_fixed" else
                               {"band_ghz": [5.5, 7.0], "min_neighbour_detuning_mhz": 100, "coupler_band_ghz": [7.0, 9.0]})
        d["leakage_channels"] = True
        d["crosstalk"] = {"drive": [[e["a"], e["b"], 0.01, 0.0] for e in d["edges"][:min(8, len(d["edges"]))]]}
    d["_nominal"] = nom
    return d

# ----------------------------------------------------------------------------- calibration
def coherence_limited_error(tau_us, t1_us, t2_us):
    """Average infidelity of idling for tau (T12 convention): [2(1-e^{-t/T2}) + (1-e^{-t/T1})]/6."""
    return (2.0 * (1.0 - math.exp(-tau_us / t2_us)) + (1.0 - math.exp(-tau_us / t1_us))) / 6.0

def lognormal(rng, nominal, sigma_ln):
    return nominal * math.exp(rng.gauss(0.0, sigma_ln)) if sigma_ln > 0 else nominal

def readout_matrix(rng, fidelity):
    e = 1.0 - fidelity  # nominal average assignment error = (M01 + M10)/2
    m01 = round(min(0.1, max(0.002, rng.gauss(e, 0.3 * e))), 6)
    m10 = round(min(0.1, max(0.002, rng.gauss(e * 1.3, 0.3 * e * 1.3))), 6)  # |1> reads worse (T1 during readout)
    return [[round(1.0 - m01, 6), m01], [m10, round(1.0 - m10, 6)]]  # rows sum to 1 exactly at 6 dp

def ion_modes(n, omega_z_mhz, omega_r_mhz):
    """Equilibrium positions and axial/radial normal modes of a linear chain (spec 09 §5.4, T06 §1)."""
    u = [2.018 * n ** -0.559 * (i - (n + 1) / 2.0) for i in range(1, n + 1)]
    for _ in range(200):  # Newton iteration on the force balance
        F = [0.0] * n
        J = [[0.0] * n for _ in range(n)]
        for i in range(n):
            F[i] = u[i]
            J[i][i] = 1.0
            for j in range(n):
                if j == i:
                    continue
                d = u[i] - u[j]
                s = 1.0 if j < i else -1.0
                F[i] -= s / (d * d)
                J[i][i] += 2.0 * s / (d ** 3)
                J[i][j] -= 2.0 * s / (d ** 3)
        # solve J du = -F (Gaussian elimination)
        A = [row[:] + [-F[i]] for i, row in enumerate(J)]
        for c in range(n):
            p = max(range(c, n), key=lambda r: abs(A[r][c])); A[c], A[p] = A[p], A[c]
            for r in range(n):
                if r != c and A[c][c] != 0:
                    f = A[r][c] / A[c][c]
                    for k in range(c, n + 1):
                        A[r][k] -= f * A[c][k]
        du = [A[i][n] / A[i][i] for i in range(n)]
        u = [u[i] + du[i] for i in range(n)]
        if max(abs(x) for x in du) < 1e-13:
            break
    Amat = [[0.0] * n for _ in range(n)]
    for i in range(n):
        for j in range(n):
            if i == j:
                Amat[i][i] = 1.0 + 2.0 * sum(1.0 / abs(u[i] - u[k]) ** 3 for k in range(n) if k != i)
            else:
                Amat[i][j] = -2.0 / abs(u[i] - u[j]) ** 3
    ratio2 = (omega_r_mhz / omega_z_mhz) ** 2
    Bmat = [[(ratio2 if i == j else 0.0) - 0.5 * (Amat[i][j] - (1.0 if i == j else 0.0)) for j in range(n)] for i in range(n)]
    def jacobi_eig(M):
        n_ = len(M); a = [row[:] for row in M]; v = [[1.0 if i == j else 0.0 for j in range(n_)] for i in range(n_)]
        for _sweep in range(100):
            off = sum(a[i][j] ** 2 for i in range(n_) for j in range(n_) if i != j)
            if off < 1e-22:
                break
            for p_ in range(n_):
                for q_ in range(p_ + 1, n_):
                    if abs(a[p_][q_]) < 1e-30:
                        continue
                    th = 0.5 * math.atan2(2 * a[p_][q_], a[q_][q_] - a[p_][p_])
                    c, s_ = math.cos(th), math.sin(th)
                    for k in range(n_):
                        akp, akq = a[k][p_], a[k][q_]
                        a[k][p_], a[k][q_] = c * akp - s_ * akq, s_ * akp + c * akq
                    for k in range(n_):
                        apk, aqk = a[p_][k], a[q_][k]
                        a[p_][k], a[q_][k] = c * apk - s_ * aqk, s_ * apk + c * aqk
                    for k in range(n_):
                        vkp, vkq = v[k][p_], v[k][q_]
                        v[k][p_], v[k][q_] = c * vkp - s_ * vkq, s_ * vkp + c * vkq
        return [a[i][i] for i in range(n_)], v
    mu, va = jacobi_eig(Amat)
    nu, vr = jacobi_eig(Bmat)
    axial = sorted([(omega_z_mhz * math.sqrt(m), [va[i][p] for i in range(n)]) for p, m in enumerate(mu)], key=lambda t: t[0])
    radial = sorted([(omega_z_mhz * math.sqrt(m), [vr[i][p] for i in range(n)]) for p, m in enumerate(nu)], key=lambda t: -t[0])
    return u, axial, radial

def build_calibration(dev, rng, created):
    nom = dev["_nominal"]; dev_id = dev["id"]; tech = dev["technology"]
    n = len(dev["qubits"])
    cal = {"device": dev_id, "timestamp": created, "qubits": {}, "edges": {}}
    if tech == "ion_chain":
        u, axial, radial = ion_modes(n, nom["omega_z_mhz"], nom["omega_r_mhz"])
        gate_mode = axial[0] if n <= 11 else radial[0]
        eta_scale = nom["eta"] / max(abs(x) for x in gate_mode[1])
        for i in range(n):
            t2e_ion = lognormal(rng, nom["t2_echo_us"], 0.2)
            q = {
                "f01_ghz": triple(nom["f_qubit_ghz"] + rng.gauss(0, 2e-9), 1e-9, "qubit_spectroscopy"),
                "anharmonicity_mhz": triple(0.0, 0.0, "n/a"),
                "t1_us": triple(1e15, 0.0, "t1"),
                "t2_echo_us": triple(t2e_ion, 0.1 * nom["t2_echo_us"], "t2_echo"),
                "t2_star_us": triple(t2e_ion * rng.uniform(0.3, 0.6), 0.1 * nom["t2_star_us"], "ramsey"),
                "thermal_population": triple(1e-5, 1e-6, "thermal_population"),
                "gate_error_1q": triple(lognormal(rng, nom["sx_err"], 0.25), 0.25 * nom["sx_err"], "rb_1q"),
                "duration_1q_ns": triple(int(nom["sx_us"] * 1000), 0, "pulses"),
                "coherent_error_fraction_1q": triple(0.0, 0.0, "default"),
                "reset_error": triple(1e-4, 2e-5, "reset_fidelity"),
                "readout_assignment": readout_matrix(rng, nom["ro_f"]),
                "readout_duration_ns": triple(int(nom["ro_us"] * 1000), 0, "pulses"),
                "readout_crosstalk_dephasing": triple(0.0, 0.0, "default"),
                "leakage_1q": triple(0.0, 0.0, "default"),
                "lamb_dicke": triple(eta_scale * abs(gate_mode[1][i]), 0.003, "model"),
            }
            cal["qubits"][str(i)] = q
        for i in range(n):
            for j in range(i + 1, n):
                err = lognormal(rng, nom["ms_err"], 0.25) * (1.0 + 0.02 * abs(i - j))
                cal["edges"][f"{i}-{j}"] = {
                    "gate_error_2q": triple(err, 0.25 * err, "rb_2q"),
                    "coherent_error_fraction_2q": triple(0.0, 0.0, "default"),
                    "leakage_2q": triple(0.0, 0.0, "default"),
                    "zz_khz": triple(0.0, 0.0, "n/a"),
                    "duration_ns": triple(int(nom["ms_us"] * 1000), 0, "pulses"),
                    "coupling_g_mhz": triple(0.0, 0.0, "n/a"),
                }
        cal["motional"] = {
            "equilibrium_positions_l": [round_sig(x, 8) for x in u],
            "axial_modes_mhz": [round_sig(w, 8) for w, _ in axial],
            "radial_modes_mhz": [round_sig(w, 8) for w, _ in radial],
            "gate_mode": {"axis": "axial" if n <= 11 else "radial", "index": 0,
                          "participation": [round_sig(x, 6) for x in gate_mode[1]]},
            "heating_quanta_per_s": triple(lognormal(rng, nom["heating"], 0.3), 0.3 * nom["heating"], "heating_rate"),
        }
        return cal
    f, alpha = assign_frequencies(dev, rng)
    t1_sig = nom.get("t1_sigma_ln", 0.30)
    for i, qd in enumerate(dev["qubits"]):
        is_coupler = qd["kind"] == "coupler"
        t1 = lognormal(rng, nom["t1_us"] * (0.5 if is_coupler else 1.0), t1_sig)
        t2e = min(lognormal(rng, nom["t2_echo_us"], 0.35), 2.0 * t1 * 0.98)
        t2s = t2e * rng.uniform(0.4, 0.8)
        sx_ns = nom["sx_ns"]
        floor1 = coherence_limited_error(sx_ns * 1e-3, t1, t2e) * 1.05
        e1 = max(lognormal(rng, nom["sx_err"], 0.25), floor1)
        pop = min(0.2, lognormal(rng, nom["thermal_pop"], 0.3))
        q = {
            "f01_ghz": triple(f[i], 2e-5, "qubit_spectroscopy"),
            "anharmonicity_mhz": triple(alpha[i] * 1000.0, 0.5, "two_photon_spectroscopy"),
            "t1_us": triple(t1, 0.05 * t1, "t1"),
            "t2_echo_us": triple(t2e, 0.08 * t2e, "t2_echo"),
            "t2_star_us": triple(t2s, 0.07 * t2s, "ramsey"),
            "thermal_population": triple(pop, 0.15 * pop, "thermal_population"),
            "gate_error_1q": triple(e1, 0.13 * e1, "rb_1q"),
            "duration_1q_ns": triple(sx_ns, 0, "pulses"),
            "coherent_error_fraction_1q": triple(0.0, 0.0, "default"),
            "reset_error": triple(lognormal(rng, nom["reset_error"], 0.3), 0.001, "reset_fidelity"),
            "readout_assignment": readout_matrix(rng, nom["ro_f"]),
            "readout_duration_ns": triple(nom["ro_ns"], 0, "pulses"),
            "readout_crosstalk_dephasing": triple(0.0, 0.0, "default"),
            "readout_f_ghz": triple(dev["readout"]["resonator_f_ghz"][i] if i < len(dev["readout"]["resonator_f_ghz"]) else 0.0, 5e-5, "resonator_spectroscopy"),
            "readout_chi_mhz": triple(nom["readout_chi_mhz"] * (1 + rng.gauss(0, 0.1)), 0.03, "dispersive_shift"),
            "readout_kappa_mhz": triple(nom["readout_kappa_mhz"] * (1 + rng.gauss(0, 0.08)), 0.2, "resonator_spectroscopy"),
            "leakage_1q": triple(lognormal(rng, nom["leak_1q"], 0.3), 0.3 * nom["leak_1q"], "leakage_rb"),
        }
        if is_coupler:
            q["readout_f_ghz"] = triple(0.0, 0.0, "n/a")
            q["readout_assignment"] = [[1.0, 0.0], [0.0, 1.0]]
        cal["qubits"][str(i)] = q
    for e in dev["edges"]:
        a, b = e["a"], e["b"]
        qa, qb = cal["qubits"][str(a)], cal["qubits"][str(b)]
        if tech == "transmon_fixed":
            dur, err_nom, gname = nom["cx_ns"], nom["cx_err"], "cx"
        else:
            dur, err_nom, gname = nom["cz_ns"], nom["cz_err"], "cz"
        floor2 = (coherence_limited_error(dur * 1e-3, qa["t1_us"][0], qa["t2_echo_us"][0]) +
                  coherence_limited_error(dur * 1e-3, qb["t1_us"][0], qb["t2_echo_us"][0])) * 1.05
        e2 = max(lognormal(rng, err_nom, 0.25), floor2)
        ed = {
            "gate_error_2q": triple(e2, 0.08 * e2, "rb_2q"),
            "coherent_error_fraction_2q": triple(0.0, 0.0, "default"),
            "leakage_2q": triple(lognormal(rng, nom["leak_2q"], 0.3), 0.3 * nom["leak_2q"], "leakage_rb"),
            "zz_khz": triple(nom["zz_khz"] * (1 + rng.gauss(0, 0.25)), 3.0, "zz_ramsey"),
            "duration_ns": triple(dur, 0, "pulses"),
            "coupling_g_mhz": triple(nom["g_mhz"] * (1 + rng.gauss(0, 0.06)), 0.2, "model"),
            "native_gate": gname,
        }
        if tech == "transmon_tunable_coupler":
            e2s = max(lognormal(rng, nom["siswap_err"], 0.25), floor2)
            ed["siswap"] = {"gate_error_2q": triple(e2s, 0.08 * e2s, "rb_2q"), "duration_ns": triple(nom["siswap_ns"], 0, "pulses")}
            ed["coupler"] = e["coupler"]
        cal["edges"][f"{a}-{b}"] = ed
    return cal

# ----------------------------------------------------------------------------- pulses.json (spec 10 §6)
def build_pulses(dev, cal, rng):
    tech = dev["technology"]; dev_id = dev["id"]; nom = dev["_nominal"]
    p = {"device": dev_id, "frames": {}, "templates": {}, "defcals": []}
    if tech == "ion_chain":
        n = len(dev["qubits"])
        p["frames"]["g"] = {"frequency_ghz": "device.ion.f_qubit_ghz", "channel": "g"}
        for i in range(n):
            p["frames"][f"r{i}"] = {"frequency_ghz": f"cal.qubits.{i}.f01_ghz", "channel": f"r[{i}]"}
        mode = cal["motional"]["gate_mode"]
        T_us = nom["ms_us"]; K = 1
        delta_rad_s = 2 * math.pi * K / (T_us * 1e-6)
        p["templates"]["ms_bichromatic"] = {
            "description": "Bichromatic Raman drive at f_q ± (f_mode + delta) on both ions; gaussian_square envelope with 10 us edges; closed loop delta*T = 2*pi*K (T06 §6).",
            "mode": {"axis": mode["axis"], "index": mode["index"]}, "K": K, "T_us": T_us,
            "delta_khz": round_sig(delta_rad_s / 2 / math.pi / 1e3), "edge_us": 10.0,
            "amp_rule": "Omega = sqrt(theta*delta/(eta_i*eta_j*T))  [rad/s]  (T06 (6.5): theta = eta_i*eta_j*Omega^2*T/delta)",
        }
        p["templates"]["raman_pi_half"] = {"wf": "gaussian_square", "T_us": nom["sx_us"], "edge_us": 1.0, "amp": 0.5}
        for i in range(n):
            eta_i = cal["qubits"][str(i)]["lamb_dicke"][0]
            for g in ("rx", "ry"):
                p["defcals"].append({"gate": g, "qubits": [i], "params": ["theta"],
                    "instructions": [{"play": {"ch": f"r[{i}]", "wf": "gaussian_square", "T_ns": int(nom["sx_us"] * 1000),
                                               "edge_ns": 1000, "amp": "0.5*theta/(pi/2)", "phase": 0.0 if g == "rx" else 1.5707963268}}]})
            p["defcals"].append({"gate": "rz", "qubits": [i], "params": ["theta"],
                                 "instructions": [{"shift_phase": {"ch": f"r[{i}]", "value": "-theta"}}]})
            p["defcals"].append({"gate": "measure", "qubits": [i],
                                 "instructions": [{"play": {"ch": "detect", "wf": "constant", "T_ns": int(nom["ro_us"] * 1000), "amp": 1.0}},
                                                  {"acquire": {"ch": f"a[{i}]", "T_ns": int(nom["ro_us"] * 1000), "kind": "photon_count", "threshold": "device.readout"}}]})
            p["defcals"].append({"gate": "reset", "qubits": [i],
                                 "instructions": [{"play": {"ch": "pump", "wf": "constant", "T_ns": 10000, "amp": 1.0}}]})
        for key in cal["edges"]:
            i, j = (int(x) for x in key.split("-"))
            eta_i = cal["qubits"][str(i)]["lamb_dicke"][0]; eta_j = cal["qubits"][str(j)]["lamb_dicke"][0]
            # T06 (6.5): XX(theta) = exp(-i theta/2 XX) needs |2 Phi| = theta/2 with Phi = g^2 T/delta, g = eta*Omega/2,
            # hence theta = eta_i*eta_j*Omega^2*T/delta (no factor pi); at theta = pi/2 and delta*T = 2*pi*K this is
            # Omega = pi*sqrt(K)/(eta*T), T06 (6.6).
            omega = math.sqrt((math.pi / 2) * delta_rad_s / (eta_i * eta_j * T_us * 1e-6))
            p["defcals"].append({"gate": "ms", "qubits": [i, j], "params": ["theta"], "ref": "ms_bichromatic",
                                 "params_from": f"cal.edges.{key}",
                                 "omega_rad_s_at_pi_2": round_sig(omega), "omega_over_2pi_khz": round_sig(omega / 2 / math.pi / 1e3)})
        return p
    # transmons
    T1q = nom["sx_ns"]; sigma = T1q / 4.0
    ctrl_of = {}  # target -> list of controls (for virtual-Z frame shifts on u channels)
    for e in dev["edges"]:
        if tech == "transmon_fixed":
            ctrl_of.setdefault(e["b"], []).append(e["a"])
    for i, qd in enumerate(dev["qubits"]):
        p["frames"][f"d{i}"] = {"frequency_ghz": f"cal.qubits.{i}.f01_ghz", "channel": f"d[{i}]"}
        if qd["kind"] == "data":
            p["frames"][f"m{i}"] = {"frequency_ghz": f"cal.qubits.{i}.readout_f_ghz", "channel": f"m[{i}]"}
    if tech == "transmon_fixed":
        for e in dev["edges"]:
            p["frames"][f"u{e['a']}_{e['b']}"] = {"frequency_ghz": f"cal.qubits.{e['b']}.f01_ghz", "channel": f"u[{e['a']},{e['b']}]"}
    p["templates"]["cr_echo"] = {"description": "Echoed cross-resonance with active cancellation tone (spec 10 §6.3, T05 §8)",
                                 "wf": "gaussian_square", "sigma_ns": 16, "rise_ns": 32,
                                 "T_cr_rule": "cal.edges.c-t.duration_ns/2 - T_x", "corrections": ["rz(-pi/2) c", "sx t", "rz t"]}
    p["templates"]["cr_echo_bare"] = {"description": "ecr: steps 1-4 of cr_echo without the single-qubit corrections"}
    p["templates"]["cz_adiabatic"] = {"description": "Slepian flux pulse detuning the higher-frequency qubit toward |11>-|20> (spec 10 §6.4)",
                                      "wf": "slepian", "T_ns": nom.get("cz_ns", 40), "phase_target_rad": 3.14159265, "leakage_max": 1e-3}
    p["templates"]["siswap_resonant"] = {"description": "Coupler flux pulse bringing the pair into resonance for T = pi/(4 g_eff) (spec 10 §6.5)",
                                         "wf": "gaussian_square", "edge_ns": 5, "T_ns": nom.get("siswap_ns", 32)}
    p["templates"]["measure_dispersive"] = {"wf": "gaussian_square", "sigma_ns": 20, "rise_ns": 40, "acquire_delay_ns": 100,
                                            "weights": "matched", "amp": 0.10}
    p["templates"]["reset_active"] = {"kind": "measure_conditional_x", "feedforward_ns": 200}
    for i, qd in enumerate(dev["qubits"]):
        alpha_ghz = cal["qubits"][str(i)]["anharmonicity_mhz"][0] / 1000.0
        beta_ns = round_sig(-0.5 / (2 * math.pi * alpha_ghz), 4)  # DRAG beta = lambda/(2 pi |alpha|), lambda = 0.5
        amp_sx = round_sig(0.182 * (1 + rng.gauss(0, 0.05)), 4)
        p["defcals"].append({"gate": "sx", "qubits": [i], "instructions": [
            {"play": {"ch": f"d[{i}]", "wf": "drag", "T_ns": T1q, "sigma_ns": sigma, "beta_ns": beta_ns, "amp": amp_sx}}]})
        p["defcals"].append({"gate": "x", "qubits": [i], "instructions": [
            {"play": {"ch": f"d[{i}]", "wf": "drag", "T_ns": T1q, "sigma_ns": sigma, "beta_ns": beta_ns, "amp": round_sig(2 * amp_sx, 4)}}]})
        shifts = [{"shift_phase": {"ch": f"d[{i}]", "value": "-theta"}}]
        for c in ctrl_of.get(i, []):
            shifts.append({"shift_phase": {"ch": f"u[{c},{i}]", "value": "-theta"}})
        p["defcals"].append({"gate": "rz", "qubits": [i], "params": ["theta"], "instructions": shifts})
        p["defcals"].append({"gate": "id", "qubits": [i], "instructions": [{"delay": {"ch": f"d[{i}]", "T_ns": T1q}}]})
        if qd["kind"] == "data":
            ro = cal["qubits"][str(i)]["readout_duration_ns"][0]
            p["defcals"].append({"gate": "measure", "qubits": [i], "ref": "measure_dispersive", "instructions": [
                {"play": {"ch": f"m[{i}]", "wf": "gaussian_square", "T_ns": ro, "sigma_ns": 20, "rise_ns": 40, "amp": 0.10}},
                {"acquire": {"ch": f"a[{i}]", "delay_ns": 100, "T_ns": ro, "weights": "matched"}}]})
            p["defcals"].append({"gate": "reset", "qubits": [i], "ref": "reset_active"})
    for e in dev["edges"]:
        a, b = e["a"], e["b"]; key = f"{a}-{b}"
        if tech == "transmon_fixed":
            p["defcals"].append({"gate": "cx", "qubits": [a, b], "ref": "cr_echo", "params_from": f"cal.edges.{key}",
                                 "amp_cr": round_sig(0.45 * (1 + rng.gauss(0, 0.1)), 4), "amp_cancel": round_sig(0.04 * (1 + rng.gauss(0, 0.2)), 4),
                                 "phase_cancel_rad": round_sig(rng.uniform(-0.3, 0.3), 4)})
            p["defcals"].append({"gate": "ecr", "qubits": [a, b], "ref": "cr_echo_bare", "params_from": f"cal.edges.{key}"})
        else:
            p["defcals"].append({"gate": "cz", "qubits": [a, b], "ref": "cz_adiabatic", "params_from": f"cal.edges.{key}",
                                 "flux_channel": f"f[{e['coupler']}]", "amp": round_sig(0.35 * (1 + rng.gauss(0, 0.08)), 4)})
            p["defcals"].append({"gate": "siswap", "qubits": [a, b], "ref": "siswap_resonant", "params_from": f"cal.edges.{key}.siswap",
                                 "flux_channel": f"f[{e['coupler']}]", "amp": round_sig(0.28 * (1 + rng.gauss(0, 0.08)), 4)})
    return p

# ----------------------------------------------------------------------------- wiring.json (spec 11 §9)
def build_wiring(dev):
    tech = dev["technology"]; dev_id = dev["id"]
    w = {"device": dev_id, "layout": "ion_lab_11" if tech == "ion_chain" else "sc_lab_standard", "lines": []}
    if tech == "ion_chain":
        n = len(dev["qubits"])
        w["lines"].append({"id": "raman_global", "channel": "g", "chain": "optical_raman_global"})
        for i in range(n):
            w["lines"].append({"id": f"raman_r{i}", "channel": f"r[{i}]", "chain": "optical_raman_individual"})
        w["lines"].append({"id": "detect", "channel": "detect", "chain": "optical_detection"})
        w["lines"].append({"id": "pump", "channel": "pump", "chain": "optical_pumping"})
        w["lines"].append({"id": "trap_rf", "channel": "rf", "chain": "trap_rf_drive"})
        w["lines"].append({"id": "trap_dc", "channel": f"dc[0..{2 * n + 7}]", "chain": "trap_dc_electrodes"})
        w["lines"].append({"id": "imaging", "channel": f"a[0..{n - 1}]", "chain": "optical_imaging"})
        return w
    for q in dev["qubits"]:
        i = q["index"]
        if q["kind"] == "data":
            w["lines"].append({"id": f"drive_q{i}", "channel": f"d[{i}]", "chain": "drive_std",
                               "attenuation_db": {"PT2": 20, "STILL": 0, "CP": 0, "MXC": 20}})
        if tech != "transmon_fixed":
            w["lines"].append({"id": f"flux_{'c' if q['kind'] == 'coupler' else 'q'}{i}", "channel": f"f[{i}]", "chain": "flux_std",
                               "attenuation_db": {"PT2": 20, "STILL": 0, "CP": 0, "MXC": 0}, "lpf_ghz": 1.0})
    for fl in dev["readout"]["feedlines"]:
        qs = fl["qubits"]; rng_s = f"{qs[0]}..{qs[-1]}" if len(qs) > 1 else f"{qs[0]}"
        w["lines"].append({"id": f"ro_in_f{fl['id']}", "channel": f"m[{rng_s}]", "chain": "readout_in_std",
                           "attenuation_db": {"PT2": 20, "STILL": 10, "CP": 20, "MXC": 20}, "lpf_ghz": 10.0})
        w["lines"].append({"id": f"ro_out_f{fl['id']}", "channel": f"a[{rng_s}]", "chain": "readout_out_std", "preamp": "twpa",
                           "isolators": 2, "hemt": {"gain_db": 38, "noise_k": 2.5, "dissipation_mw": 12},
                           "rt_amp": {"gain_db": 30, "noise_k": 80}})
        w["lines"].append({"id": f"pump_f{fl['id']}", "channel": f"pump[{fl['id']}]", "chain": "pump_std",
                           "attenuation_db": {"PT2": 20, "STILL": 0, "CP": 0, "MXC": 20}})
    n_dc = 12 if tech == "transmon_fixed" else 24
    w["lines"].append({"id": "dc_loom_0", "channel": f"dc[0..{n_dc - 1}]", "chain": "dc_loom_std"})
    return w

# ----------------------------------------------------------------------------- main
def generate(dev_id, seed, out_dir, created):
    rng = random.Random(f"{dev_id}:{seed}")
    dev = build_device(dev_id, rng)
    cal = build_calibration(dev, rng, created)
    pul = build_pulses(dev, cal, rng)
    wir = build_wiring(dev)
    dev_out = {k: v for k, v in dev.items() if not k.startswith("_")}
    dev_out["generator"] = {"tool": "gencal.py", "seed": seed}
    base = os.path.join(out_dir, dev_id)
    write_json(os.path.join(base, "device.json"), envelope("device", dev_out, created))
    write_json(os.path.join(base, "calibration.json"), envelope("calibration", cal, created))
    write_json(os.path.join(base, "pulses.json"), envelope("pulses", pul, created))
    write_json(os.path.join(base, "wiring.json"), envelope("wiring", wir, created))
    return dev_out, cal, pul, wir

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Assets", "Devices"))
    ap.add_argument("--device", default=None, help="one device id (default: all)")
    ap.add_argument("--created", default="2026-09-17T00:00:00Z")
    a = ap.parse_args(argv)
    ids = [a.device] if a.device else DEVICE_IDS
    for d in ids:
        dev, cal, pul, wir = generate(d, a.seed, a.out, a.created)
        print(f"{d}: {len(dev['qubits'])} qubits, {len(dev['edges'])} edges, {len(cal['edges'])} calibrated pairs, "
              f"{len(pul['defcals'])} defcals, {len(wir['lines'])} lines")
    return 0

if __name__ == "__main__":
    sys.exit(main())
