#pragma once
// Spec 12 §4 — IQ mixer / up-converter `iq_mixer`. With the LO of a `sg_mw` and the AWG streams
// s = I + iQ, the complex envelope of the RF output around f_LO is
//   z(t) = c · [ s(t) + ε_LO·V_fs + ε_img · conj(s(t)) ] · e^{iφ_n(t)},
// the envelope form of §4's s_RF (T07 (5.1)): c the conversion loss, ε_LO the LO leakage relative
// to a full-scale carrier, ε_img the image sideband from gain/phase imbalance (IRR ≃ (ε² + φ²)/4,
// T07 §5), φ_n the LO phase noise. The AWG predistortion of §13 (DC offsets, amplitude ratio,
// phase skew) enters through s and nulls both spurs. Class Model.
#include "Instruments/Awg.hpp"
#include "Instruments/Generator.hpp"

namespace qlab::instr {

// Equivalent hardware imbalance of an image coefficient ε_img: γ = (1 − ε)/(1 + ε) is the Q-path
// gain and phase relative to I (T07 §5).
struct IqImbalance {
    double gainDb = 0.0;
    double phaseDeg = 0.0;
};

class IqMixer final : public InstrumentBase, public ISignalSource {
  public:
    explicit IqMixer(std::uint32_t index = 0);
    static SettingSchema makeSchema();

    // Direct links to the instruments feeding this mixer (the registry sets them next to the
    // routing edges). They give the readings and the calibration tool the AWG corrections, the IF
    // and the LO frequency; the signal itself always flows through the routing matrix.
    void attach(const Awg* awg, std::uint32_t awgPort, const Generator* lo);
    const Awg* awg() const { return awg_; }
    std::uint32_t awgPort() const { return awgPort_; }
    const Generator* lo() const { return lo_; }

    std::string ifNode() const { return id().toString() + ".if"; }
    std::string loNode() const { return id().toString() + ".lo"; }
    std::string rfNode() const { return id().toString() + ".rf"; }

    Complex leakageCoefficient() const; // ε_LO (complex, relative to full scale)
    Complex imageCoefficient() const;   // ε_img (complex)
    IqImbalance imbalance() const;
    // Residual spurs in dBc with the attached AWG's present corrections (the intrinsic values
    // without an AWG): carrier leakage relative to a full-scale carrier, image relative to the
    // wanted sideband.
    double loLeakageDbc() const;
    double imageDbc() const;

    // Port "rf". Errors: NotBound without a routing matrix.
    Result<Signal> signal(std::string_view port, const SignalRequest& request) const override;
    double sampleRateHz(std::string_view port) const override;         // the rate of the IF input
    std::optional<double> query(std::string_view path) const override; // lo_leak, image_rej

  protected:
    Result<Trace> doAcquire(const ChannelDesc& channel, AcquireContext& ctx) override;
    bool triggerSourceSet(const SettingValues&) const override { return false; }

  private:
    const Awg* awg_ = nullptr;
    std::uint32_t awgPort_ = 0;
    const Generator* lo_ = nullptr;
};

} // namespace qlab::instr
