#pragma once
// Spec 12 §8 — pressure gauge, ³He flow meter and RF power meter. The first two read the
// gas-handling state of the cryo model (cryo::GhsSnapshot in the Environment); the power meter
// reads the mean power of a routed signal node, 10 log10(P̄ / 1 mW). Class Model.
#include "Instruments/InstrumentBase.hpp"
#include <deque>

namespace qlab::instr {

// A reading with its time, kept as a strip-chart history.
struct ScalarReading {
    double timeS = 0.0;
    double value = 0.0;
    double sigma = 0.0;
    bool outOfRange = false;
};

// Common strip-chart behaviour of the three meters.
class StripChartInstrument : public InstrumentBase {
  public:
    std::optional<ScalarReading> lastReading() const;

  protected:
    using InstrumentBase::InstrumentBase;
    Trace chart(const ChannelDesc& channel, const AcquireContext& ctx,
                const ScalarReading& reading);
    bool triggerSourceSet(const SettingValues&) const override { return false; }

  private:
    mutable std::mutex historyMu_;
    std::deque<ScalarReading> history_;
};

// Pirani + cold cathode on the vacuum can, capacitance manometers on the still, condensing line
// and dump: 1e−8 … 3000 mbar.
class PressureGauge final : public StripChartInstrument {
  public:
    explicit PressureGauge(std::uint32_t index = 0);
    static SettingSchema makeSchema();
    // Gauge technology in use for a node and pressure: "cold_cathode", "pirani" or "capacitance".
    static std::string_view technology(std::string_view node, double mbar);
    std::optional<double> query(std::string_view path) const override; // p

  protected:
    Result<Trace> doAcquire(const ChannelDesc& channel, AcquireContext& ctx) override;
};

// Thermal mass-flow meter in the circulation loop: ṅ₃ in mmol/s, 0 … 5.
class FlowMeter final : public StripChartInstrument {
  public:
    explicit FlowMeter(std::uint32_t index = 0);
    static SettingSchema makeSchema();
    std::optional<double> query(std::string_view path) const override; // n3

  protected:
    Result<Trace> doAcquire(const ChannelDesc& channel, AcquireContext& ctx) override;
};

// RF power at a routing node, −70 … +20 dBm, over the head's calibrated band.
class PowerMeter final : public StripChartInstrument {
  public:
    explicit PowerMeter(std::uint32_t index = 0);
    static SettingSchema makeSchema();
    // Point-to-point scatter of a reading at an averaging time, in dB: 0.02 dB at 100 ms, improving
    // as 1/√t. The head's quoted `uncertainty_db` is systematic and is not part of this.
    static double repeatabilityDb(double averagingMs);
    std::optional<double> query(std::string_view path) const override; // p, P_dBm

  protected:
    Result<Trace> doAcquire(const ChannelDesc& channel, AcquireContext& ctx) override;
};

} // namespace qlab::instr
