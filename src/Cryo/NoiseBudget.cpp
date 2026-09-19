#include "Cryo/NoiseBudget.hpp"
#include "Cryo/Physics.hpp"
#include <cmath>

namespace qlab::cryo {

double thermalPhotons(double f, double T) {
    return phys::thermalPhotons(f, T);
}
double effectiveTemperature(double f, double n) {
    return phys::effectiveTemperature(f, n);
}

LineNoise NoiseBudget::inputLine(const WiringLine& line, const StageArray& T,
                                 NoiseBudgetOptions opt) const {
    LineNoise r;
    r.lineId = line.id;
    r.f_Hz = opt.f_Hz;
    r.n_in =
        opt.n_in < 0 ? phys::thermalPhotons(opt.f_Hz, nominalTemperature(Stage::RT)) : opt.n_in;
    double n = r.n_in;
    for (const Element& e : line.elements) {
        double A_dB = 0;
        switch (e.kind) {
        case ElementKind::Attenuator:
            A_dB = e.attenuation_dB;
            break;
        case ElementKind::IrFilter:
            A_dB = 0.0;
            break; // not part of the photon budget (T07 §9)
        case ElementKind::CoaxSegment:
            if (opt.includeCableLoss)
                if (const CoaxSpec* c = coax_.find(e.coax))
                    A_dB = coaxLoss_dB(*c, e.length_m, opt.f_Hz, T[stageIndex(e.stage)]);
            break;
        default:
            break;
        }
        if (e.kind == ElementKind::Bulkhead || e.kind == ElementKind::Chip)
            continue;
        PhotonStep s;
        s.stage = e.stage;
        s.elementId = e.id;
        s.A_dB = A_dB;
        s.n_in = n;
        double A = phys::dbToLinear(A_dB);
        s.emitted = (1.0 - 1.0 / A) * phys::thermalPhotons(opt.f_Hz, T[stageIndex(e.stage)]);
        s.n_out = n / A + s.emitted;
        n = s.n_out;
        r.totalAttenuation_dB += A_dB;
        r.steps.push_back(s);
    }
    r.n_chip = n;
    r.T_eff_K = phys::effectiveTemperature(opt.f_Hz, n);
    r.P1_thermal = phys::populationFromPhotons(n);
    return r;
}

OutputChainNoise NoiseBudget::outputLine(const WiringLine& line, const StageArray& T,
                                         double f) const {
    OutputChainNoise o;
    o.lineId = line.id;
    // Forward direction chip → RT: Friis T_sys = Σ T_i / G_before_i, with passive lossy elements at
    // temperature T_s contributing T_s (L − 1) referred to their input (T07 §10).
    double Gbefore = 1.0;
    double Tsys = 0.0;
    for (const Element& e : line.elements) {
        double Ts = T[stageIndex(e.stage)];
        switch (e.kind) {
        case ElementKind::Isolator:
        case ElementKind::Circulator: {
            double L = phys::dbToLinear(e.attenuation_dB);
            double Tn = Ts * (L - 1.0);
            o.contributions_K.push_back({e.id, Tn / Gbefore});
            Tsys += Tn / Gbefore;
            Gbefore /= L;
            break;
        }
        case ElementKind::CoaxSegment: {
            const CoaxSpec* c = coax_.find(e.coax);
            double loss = c ? coaxLoss_dB(*c, e.length_m, f, Ts) : 0.0;
            double L = phys::dbToLinear(loss);
            double Tn = Ts * (L - 1.0);
            o.contributions_K.push_back({e.id, Tn / Gbefore});
            Tsys += Tn / Gbefore;
            Gbefore /= L;
            break;
        }
        case ElementKind::Preamp:
        case ElementKind::Amplifier: {
            double G = phys::dbToLinear(e.gain_dB);
            o.contributions_K.push_back({e.id, e.noiseTemperature_K / Gbefore});
            Tsys += e.noiseTemperature_K / Gbefore;
            Gbefore *= G;
            break;
        }
        default:
            break;
        }
    }
    o.T_sys_K = Tsys;
    o.gainTotal_dB = phys::linearToDb(Gbefore);
    double Tq = phys::kPlanck_Js * f / (2.0 * phys::kBoltzmann_JK); // half-photon quantum limit
    o.quantumEfficiency = Tsys > 0 ? std::min(1.0, Tq / Tsys) : 1.0;
    // Back-action: photons from the HEMT input (at its stage temperature, T_N dominated) travelling
    // backwards through the isolators' reverse isolation to the chip, plus the MXC's own emission.
    double nWarm = 0.0;
    double isoTotal_dB = 0.0;
    Stage hemtStage = Stage::PT2;
    for (const Element& e : line.elements) {
        if (e.kind == ElementKind::Isolator || e.kind == ElementKind::Circulator)
            isoTotal_dB += e.isolation_dB;
        if (e.kind == ElementKind::Amplifier) {
            hemtStage = e.stage;
            break;
        }
    }
    nWarm = phys::thermalPhotons(f, T[stageIndex(hemtStage)]);
    o.n_backaction =
        nWarm * phys::dbToLinear(-isoTotal_dB) + phys::thermalPhotons(f, T[stageIndex(Stage::MXC)]);
    return o;
}

std::vector<LineNoise> NoiseBudget::allInputs(const Wiring& w, const StageArray& T,
                                              NoiseBudgetOptions opt) const {
    std::vector<LineNoise> v;
    for (auto& l : w.lines)
        if (l.kind != LineKind::ReadoutOut && l.kind != LineKind::DC)
            v.push_back(inputLine(l, T, opt));
    return v;
}

} // namespace qlab::cryo
