#pragma once
// Spec 12 §9 — DC current source for the flux lines. A current I through a line of mutual
// inductance M (device `flux_M_pH`, default 2 pH) threads Φ = M I + Φ_offset through the SQUID, and
// the qubit follows spec 09 §5.1 / T05 (4.1):
//   E_J(Φ) = E_JΣ |cos(πΦ/Φ0)| √(1 + d² tan²(πΦ/Φ0)),   f01(Φ) from the charge-basis spectrum.
// (E_JΣ, E_C) are solved from the calibrated (f01, α), taken to be measured at zero bias current.
// The `sweep` channel is the flux-spectroscopy arc f01(Φ/Φ0) with the asymptotic theory curve
// √(8 E_J E_C) − E_C alongside. Numerical eigenvalues over a Model M: class Model (spec 00 §5).
#include "Instruments/InstrumentBase.hpp"

namespace qlab::instr {

class DcSource final : public InstrumentBase {
public:
    static constexpr std::uint32_t kChannels = 8;

    explicit DcSource(std::uint32_t index = 0);
    static SettingSchema makeSchema();

    // Qubit biased by output k: bindings().channels[k] (its f[i] index), else k.
    std::uint32_t qubitOfChannel(std::uint32_t k) const;
    double currentA(std::uint32_t k) const;
    // Φ/Φ0 at the SQUID for a current on output k.
    double fluxQuanta(double currentA) const;
    // f01 of the qubit on output k at that current (charge-basis spectrum), and the asymptotic
    // transmon formula for the same E_J(Φ), E_C. Errors: NotBound, BadInput (no calibration entry).
    Result<double> qubitFrequencyHz(std::uint32_t k, double currentA) const;
    Result<double> theoryFrequencyHz(std::uint32_t k, double currentA) const;
    std::optional<double> query(std::string_view path) const override; // ch[k].I, ch[k].V, ch[k].f01

protected:
    Result<Trace> doAcquire(const ChannelDesc& channel, AcquireContext& ctx) override;
    bool triggerSourceSet(const SettingValues&) const override { return false; }
    void settingChanged(std::string_view key) override;

private:
    struct Junction { double ejSumHz = 0.0, ecHz = 0.0; }; // E_JΣ/h, E_C/h
    Result<Junction> junction(std::uint32_t k) const;
    mutable std::mutex cacheMu_;
    mutable std::map<std::uint32_t, std::pair<const hw::Calibration*, Junction>> cache_;
    mutable std::shared_ptr<const hw::Calibration> cacheOwner_; // keeps the cached calibration's address alive
};

} // namespace qlab::instr
