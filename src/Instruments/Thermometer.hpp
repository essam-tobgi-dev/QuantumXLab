#pragma once
// Spec 12 §8, T08 §8 — resistance thermometry. The bridge excites the sensor with power P, which
// warms it above its stage across a Kapitza boundary resistance R_K ∝ T⁻³,
//   T_sensor = (T_stage⁴ + 4P/k)^{1/4}      (→ ΔT = P R_K, R_K = 1/(k T³), for small P),
// measures R(T_sensor) with noise, and converts back through the calibration curve:
//   RuO₂    R = R0 exp[(T0/T)^{1/4}]          variable-range hopping, 10 mK – 40 K, 0.5 % of reading
//   Cernox  ln R = A + B ln T + C ln²T        1 – 325 K, 0.1 % of reading
// With the default k an excitation of 10 nW reads 2.5 mK high at 10 mK and 0.1 nW less than
// 0.05 mK (spec 12 §15). The readings are the only way to learn the stage temperatures in the
// Physical-lab workspace; the truth is `probe_thermal_truth`. Class Model.
#include "Instruments/InstrumentBase.hpp"
#include <deque>

namespace qlab::instr {

enum class SensorKind : std::uint8_t { RuO2, Cernox };

struct SensorCurve {
    SensorKind kind = SensorKind::RuO2;
    double minK() const;
    double maxK() const;
    double resistanceOhm(double temperatureK) const;
    double temperatureK(double resistanceOhm) const; // exact inverse of resistanceOhm
    // −d ln R / d ln T: converts a relative temperature error into a relative resistance error.
    double sensitivity(double temperatureK) const;
};

struct ThermometerReading {
    double timeS = 0.0;
    double temperatureK = 0.0;   // what the instrument shows
    double resistanceOhm = 0.0;  // what the bridge measured
    double stageK = 0.0;         // truth (never shown by the instrument; kept for the truth probe)
    double sensorK = 0.0;        // stage + self-heating
    double sigmaK = 0.0;         // 1σ of the reading noise
    bool outOfRange = false;
};

class Thermometer final : public InstrumentBase {
public:
    Thermometer(SensorKind kind, std::uint32_t index = 0);
    static SettingSchema makeSchema(SensorKind kind);

    SensorKind kind() const { return curve_.kind; }
    const SensorCurve& curve() const { return curve_; }
    cryo::Stage stage() const;
    // Self-heated sensor temperature for a stage temperature and excitation power.
    static double sensorTemperature(double stageK, double excitationW, double kapitzaWPerK4);
    std::optional<ThermometerReading> lastReading() const;
    std::optional<double> query(std::string_view path) const override; // T, R

protected:
    Result<Trace> doAcquire(const ChannelDesc& channel, AcquireContext& ctx) override;
    bool triggerSourceSet(const SettingValues&) const override { return false; }

private:
    SensorCurve curve_;
    mutable std::mutex historyMu_;
    std::deque<ThermometerReading> history_;
};

} // namespace qlab::instr
