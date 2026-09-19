#pragma once
// Spec 12 §13 — mixer calibration tool `tool_mixer_cal`. A guided routine on the modelled power:
//   1. null the LO leakage by scanning the AWG's I/Q DC offsets while watching the spectrum
//      analyzer's marker at f_LO (Nelder–Mead);
//   2. null the image by scanning the amplitude ratio and phase skew at f_LO − f_IF;
//   3. leave the corrections in the AWG channel settings.
// The nulls stop at the analyzer's noise floor and at the DAC resolution (an offset moves in steps
// of one LSB). Class Model.
#include "Instruments/IqMixer.hpp"
#include "Instruments/SpectrumAnalyzer.hpp"
#include <functional>

namespace qlab::instr {

struct MixerCalOptions {
    int maxEvaluations = 400;             // per stage
    double offsetStepV = 0.005;           // initial simplex edge for the DC offsets
    double ratioStep = 0.02;              // initial simplex edge for the amplitude ratio
    double skewStepDeg = 1.0;             // initial simplex edge for the phase skew
    double minCarrierAboveFloorDb = 20.0; // a calibration tone must be playing
};

struct MixerCalReport {
    double loHz = 0.0, ifHz = 0.0;
    double carrierDbm = 0.0;
    double loBeforeDbc = 0.0, loAfterDbc = 0.0; // relative to the measured carrier
    double imageBeforeDbc = 0.0, imageAfterDbc = 0.0;
    IqCorrection correction; // what was written into the AWG channel
    int evaluations = 0;     // analyzer readings taken
    FidelityClass cls = FidelityClass::Model;
    std::vector<std::string> log; // one line per step, for the guided panel
};

class MixerCalibration {
  public:
    MixerCalibration(Awg& awg, std::uint32_t port, IqMixer& mixer, SpectrumAnalyzer& analyzer)
        : awg_(awg), port_(port), mixer_(mixer), analyzer_(analyzer) {}

    // Errors: NotBound (mixer has no LO attached), NoSignal (no carrier above the floor), and
    // whatever the analyzer reports. On an error the AWG keeps the corrections it had.
    Result<MixerCalReport> run(const MixerCalOptions& options = {});

  private:
    Awg& awg_;
    std::uint32_t port_;
    IqMixer& mixer_;
    SpectrumAnalyzer& analyzer_;
};

// Nelder–Mead downhill simplex (reflection 1, expansion 2, contraction ½, shrink ½). Stops when the
// simplex is smaller than `tolX` in every coordinate (relative to `steps`) or after
// `maxEvaluations`.
struct SimplexResult {
    std::vector<double> x;
    double value = 0.0;
    int evaluations = 0;
    bool converged = false;
};
SimplexResult nelderMead(const std::function<double(const std::vector<double>&)>& f,
                         std::vector<double> x0, const std::vector<double>& steps,
                         double tolX = 1e-4, int maxEvaluations = 400);

} // namespace qlab::instr
