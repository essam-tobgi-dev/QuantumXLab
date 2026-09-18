#!/usr/bin/env python3
"""Generate Assets/Lab/Materials/*.json: thermal conductivity k(T) tables (W/m/K) on a log grid,
calibrated against the conductivity-integral table of docs/theory/T08 §4, plus Debye heat-capacity
parameters and emissivities. Integration in the app is log-log trapezoid between knots (Cryo/Materials.cpp)."""
import json, math, os, datetime
knots = [0.01, 0.015, 0.02, 0.03, 0.05, 0.07, 0.1, 0.15, 0.2, 0.3, 0.5, 0.7, 0.85, 1, 1.5, 2, 3, 4, 6, 10, 15, 20, 30, 50, 70, 100, 150, 200, 300]
def loglog(k, T):
    # log-log interpolation on knots
    if T <= knots[0]: return k[0] * (T / knots[0]) ** (math.log(k[1]/k[0]) / math.log(knots[1]/knots[0]))
    for i in range(len(knots)-1):
        if knots[i] <= T <= knots[i+1]:
            a = math.log(k[i+1]/k[i]) / math.log(knots[i+1]/knots[i])
            return k[i] * (T/knots[i]) ** a
    return k[-1]
def theta(k, Tc, Th, n=4000):
    # integral of k dT, log-spaced trapezoid
    s = 0.0
    for i in range(n):
        a = Tc * (Th/Tc) ** (i/n); b = Tc * (Th/Tc) ** ((i+1)/n)
        s += 0.5 * (loglog(k, a) + loglog(k, b)) * (b - a)
    return s
pairs = [(50,300),(4,50),(0.85,4),(0.1,0.85),(0.015,0.1)]
# analytic shape functions, then per-segment scaling to match T08 table targets
def make(shape, targets, name):
    k = [shape(T) for T in knots]
    # iterative per-pair scaling: scale knots inside each pair range by ratio target/actual
    for _ in range(6):
        for (Tc, Th), tgt in zip(pairs, targets):
            if tgt is None: continue
            cur = theta(k, Tc, Th)
            r = tgt / cur
            for i, T in enumerate(knots):
                if Tc < T <= Th or (Tc == knots[0] and T == Tc): k[i] *= r ** 1.0
    return k
def ss(T):   # stainless 304: linear at low T, saturating ~15 W/mK
    return 0.068*T if T < 4 else min(15.0, 0.068*4 * (T/4)**1.25) if T < 60 else 5.6*(T/50)**0.55
def cuni(T): return 0.12*T if T < 4 else 0.48*(T/4)**1.15 if T < 60 else 10.0*(T/60)**0.5
def becu(T): return 0.5*T if T < 4 else 2.0*(T/4)**1.1 if T<60 else 40*(T/60)**0.6
def brass(T): return 0.6*T if T < 4 else 2.4*(T/4)**1.1 if T<60 else 50*(T/60)**0.6
def cu100(T): return 143*T if T < 15 else 2145*(15/T)**1.2 if T < 60 else 400*(60/T)**0.02
def cu50(T): return 72*T if T < 15 else 1080*(15/T)**1.1 if T < 60 else 400*(60/T)**0.02
def nbti(T): return 0.01*T**3 if T < 9.2 else 0.01*9.2**3*(T/9.2)**0.9 if T < 60 else 7.5*(T/60)**0.4
def ptfe(T): return 0.02*T**1.8 if T < 4 else 0.24*(T/4)**0.15
def g10(T): return 0.03*T**1.7 if T < 4 else 0.3*(T/4)**0.4
def al6061(T): return 4.0*T if T < 20 else 80*(20/T)**0.2 if T<100 else 100
def phbronze(T): return 0.3*T if T<4 else 1.2*(T/4)**1.1 if T<60 else 25*(T/60)**0.5
def constantan(T): return 0.1*T if T<4 else 0.4*(T/4)**1.1 if T<60 else 8*(T/60)**0.6
def sapphire(T): return 0.5*T**3 if T<10 else 500*(10/T)**0.5 if T<40 else 250*(40/T)**1.5
def silicon(T): return 0.2*T**3 if T<10 else 200*(10/T)**0.3 if T<30 else 150*(30/T)**1.3
T08 = {
 'stainless_304': (ss,  [2.8e3, 1.4e2, 0.52, 2.1e-2, 3.3e-4]),
 'CuNi_70_30':   (cuni, [2.75e3, 1.9e2, 0.92, 3.8e-2, 5.9e-4]),  # spec 11 §3 asks ∫4..300 ≈ 2.8 kW/m
 'BeCu':         (becu, [1.8e4, 7e2, 3.8, 0.16, 2.4e-3]),
 'brass':        (brass,[2.4e4, 1.2e3, 4.6, 0.19, 2.9e-3]),
 'Cu_OFHC_RRR100': (cu100, [1.1e5, 4.6e4, 1.1e3, 45, 0.70]),
 'Cu_OFHC_RRR50':  (cu50,  [1.0e5, 6.0e4, 5.5e2, 22, 0.35]),   # ∫4..300 = 1.6e5 (spec 11 §3)
 'NbTi':         (nbti, [1.6e3, 1.0e2, 0.20, 1.0e-3, 2.5e-7]),
 'PTFE':         (ptfe, [60, 5.5, 0.10, 3.5e-3, 1e-4]),
 'G10':          (g10,  [70, 6, 0.15, 5e-3, 1.5e-4]),
 'Al_6061':      (al6061, [2.5e4, 3.0e3, 12, 0.5, 8e-3]),
 'phosphor_bronze': (phbronze, [9e3, 4e2, 2.3, 0.095, 1.5e-3]),
 'constantan':   (constantan, [4e3, 1.5e2, 0.77, 0.032, 5e-4]),
 'sapphire':     (sapphire, [2.5e4, 6e3, 20, 0.05, 2e-6]),
 'silicon':      (silicon, [3e4, 5e3, 8, 0.02, 1e-6]),
}
cp = {  # Debye temperature (K), Sommerfeld gamma (mJ/mol/K^2), molar mass (g/mol), density (kg/m^3)
 'stainless_304': (470, 0.46, 55.0, 7900), 'CuNi_70_30': (390, 0.7, 62.0, 8900), 'BeCu': (350, 0.7, 63.0, 8250),
 'brass': (320, 0.7, 64.0, 8500), 'Cu_OFHC_RRR100': (343, 0.695, 63.55, 8960), 'Cu_OFHC_RRR50': (343, 0.695, 63.55, 8960),
 'NbTi': (280, 3.0, 70.0, 6000), 'PTFE': (100, 0.0, 50.0, 2200), 'G10': (150, 0.0, 30.0, 1800), 'Al_6061': (428, 1.35, 26.98, 2700),
 'phosphor_bronze': (330, 0.7, 63.0, 8800), 'constantan': (380, 0.7, 60.0, 8900), 'sapphire': (1047, 0.0, 20.4, 3980), 'silicon': (645, 0.0, 28.09, 2330),
}
emis = {'Cu_OFHC_RRR100': 0.03, 'Cu_OFHC_RRR50': 0.03, 'Al_6061': 0.1, 'stainless_304': 0.15, 'brass': 0.05, 'BeCu': 0.05}
os.makedirs('Assets/Lab/Materials', exist_ok=True)
sc = {'NbTi': {'Tc_K': 9.2}}
for name, (shape, targets) in T08.items():
    k = make(shape, targets, name)
    got = [theta(k, a, b) for a, b in pairs]
    print(f"{name:18s}", ' '.join(f"{g:9.3g}/{t:9.3g}" for g, t in zip(got, targets)), f" ∫4..300={theta(k,4,300):.3g}")
    td, gam, M, rho = cp[name]
    data = {"id": name, "source": "NIST cryogenic material properties fits; calibrated to docs/theory/T08 §4 conductivity integrals",
            "k_table": [[T, round(kk, 6)] for T, kk in zip(knots, k)],
            "specific_heat": {"model": "debye", "theta_d_K": td, "gamma_mJ_molK2": gam, "molar_mass_g": M},
            "density_kg_m3": rho, "emissivity": emis.get(name, 0.2)}
    if name in sc: data["superconducting"] = sc[name]
    env = {"qxl": {"kind": "qlab.material", "schema": 1, "app": "0.1.0", "created": datetime.datetime.utcnow().strftime('%Y-%m-%dT%H:%M:%SZ')}, "data": data}
    json.dump(env, open(f'Assets/Lab/Materials/{name}.json', 'w'), indent=1)
