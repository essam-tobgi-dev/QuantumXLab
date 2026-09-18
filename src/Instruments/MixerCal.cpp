#include "Instruments/MixerCal.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace qlab::instr {

SimplexResult nelderMead(const std::function<double(const std::vector<double>&)>& f, std::vector<double> x0,
                         const std::vector<double>& steps, double tolX, int maxEvaluations) {
    const std::size_t n = x0.size();
    SimplexResult out;
    struct Vertex { std::vector<double> x; double v; };
    std::vector<Vertex> simplex;
    auto eval = [&](const std::vector<double>& x) { ++out.evaluations; return f(x); };
    simplex.push_back({x0, eval(x0)});
    for (std::size_t i = 0; i < n; ++i) {
        std::vector<double> x = x0;
        x[i] += steps[i];
        simplex.push_back({x, eval(x)});
    }
    auto byValue = [](const Vertex& a, const Vertex& b) { return a.v < b.v; };
    while (out.evaluations < maxEvaluations) {
        std::sort(simplex.begin(), simplex.end(), byValue);
        double size = 0.0; // simplex extent relative to the initial steps
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t k = 1; k <= n; ++k) size = std::max(size, std::abs(simplex[k].x[i] - simplex[0].x[i]) / std::abs(steps[i]));
        if (size < tolX) { out.converged = true; break; }
        std::vector<double> centroid(n, 0.0);
        for (std::size_t k = 0; k < n; ++k)
            for (std::size_t i = 0; i < n; ++i) centroid[i] += simplex[k].x[i] / static_cast<double>(n);
        auto along = [&](double t) { // centroid + t (centroid − worst)
            std::vector<double> x(n);
            for (std::size_t i = 0; i < n; ++i) x[i] = centroid[i] + t * (centroid[i] - simplex[n].x[i]);
            return x;
        };
        const auto xr = along(1.0);
        const double vr = eval(xr);
        if (vr < simplex[0].v) {
            const auto xe = along(2.0);
            const double ve = eval(xe);
            simplex[n] = ve < vr ? Vertex{xe, ve} : Vertex{xr, vr};
        } else if (vr < simplex[n - 1].v) {
            simplex[n] = {xr, vr};
        } else {
            const auto xc = along(vr < simplex[n].v ? 0.5 : -0.5);
            const double vc = eval(xc);
            if (vc < std::min(vr, simplex[n].v)) simplex[n] = {xc, vc};
            else
                for (std::size_t k = 1; k <= n; ++k) { // shrink toward the best vertex
                    for (std::size_t i = 0; i < n; ++i) simplex[k].x[i] = simplex[0].x[i] + 0.5 * (simplex[k].x[i] - simplex[0].x[i]);
                    simplex[k].v = eval(simplex[k].x);
                }
        }
    }
    std::sort(simplex.begin(), simplex.end(), byValue);
    out.x = simplex[0].x;
    out.value = simplex[0].v;
    return out;
}

Result<MixerCalReport> MixerCalibration::run(const MixerCalOptions& options) {
    if (!mixer_.lo()) return fail(err::NotBound, mixer_.id().toString() + ": no LO generator is attached");
    MixerCalReport rep;
    rep.loHz = mixer_.lo()->carrierHz();
    rep.ifHz = awg_.intermediateFrequencyHz();
    if (rep.ifHz == 0.0) return fail(err::BadInput, "mixer calibration needs a non-zero IF: the image coincides with the carrier");
    const IqCorrection before = awg_.correction(port_);

    // Point the analyzer at the mixer with the three tones on screen and resolved.
    QXL_TRY(analyzer_.set("input", mixer_.rfNode()));
    QXL_TRY(analyzer_.set("center", rep.loHz));
    QXL_TRY(analyzer_.set("span", std::max(4.0 * std::abs(rep.ifHz), 10e6)));
    const double rbw = settingNumber(analyzer_.get("rbw")).value_or(1e6);
    if (rbw > std::abs(rep.ifHz) / 8.0) QXL_TRY(analyzer_.set("rbw", std::abs(rep.ifHz) / 8.0));

    std::optional<Error> failure;
    auto read = [&](double f) {
        ++rep.evaluations;
        auto p = analyzer_.markerPowerDbm(f);
        if (!p) { if (!failure) failure = p.error(); return 0.0; }
        return *p;
    };
    auto restore = [&](const Error& e) -> std::unexpected<Error> {
        (void)awg_.setCorrection(port_, before);
        return std::unexpected(e);
    };
    const double carrierHz = rep.loHz + rep.ifHz, imageHz = rep.loHz - rep.ifHz;
    rep.carrierDbm = read(carrierHz);
    const double loBefore = read(rep.loHz), imageBefore = read(imageHz);
    if (failure) return restore(*failure);
    auto floorTrace = analyzer_.markerPowerDbm(rep.loHz + 1.7 * rep.ifHz); // between the tones: the floor
    if (floorTrace && rep.carrierDbm < *floorTrace + options.minCarrierAboveFloorDb)
        return restore(Error(err::NoSignal, std::format("mixer calibration: no carrier at {:.6g} Hz — play a calibration tone on the channel",
                                                        carrierHz)));
    rep.loBeforeDbc = loBefore - rep.carrierDbm;
    rep.imageBeforeDbc = imageBefore - rep.carrierDbm;
    rep.log.push_back(std::format("carrier {:.2f} dBm at {:.6g} Hz; LO leakage {:.1f} dBc, image {:.1f} dBc",
                                  rep.carrierDbm, carrierHz, rep.loBeforeDbc, rep.imageBeforeDbc));

    // Step 1 (§13): I/Q DC offsets against the marker at f_LO.
    IqCorrection c = before;
    auto offsets = nelderMead(
        [&](const std::vector<double>& x) {
            c.offsetI = x[0];
            c.offsetQ = x[1];
            if (auto ok = awg_.setCorrection(port_, c); !ok && !failure) failure = ok.error();
            return read(rep.loHz);
        },
        {before.offsetI, before.offsetQ}, {options.offsetStepV, options.offsetStepV}, 1e-4, options.maxEvaluations);
    if (failure) return restore(*failure);
    c.offsetI = offsets.x[0];
    c.offsetQ = offsets.x[1];
    QXL_TRY(awg_.setCorrection(port_, c));
    rep.log.push_back(std::format("offsets I {:+.3f} mV, Q {:+.3f} mV after {} readings", c.offsetI * 1e3, c.offsetQ * 1e3, offsets.evaluations));

    // Step 2 (§13): amplitude ratio and phase skew against the marker at f_LO − f_IF.
    constexpr double kDeg = std::numbers::pi / 180.0;
    auto skew = nelderMead(
        [&](const std::vector<double>& x) {
            c.gainRatio = x[0];
            c.phaseSkewRad = x[1] * kDeg;
            if (auto ok = awg_.setCorrection(port_, c); !ok && !failure) failure = ok.error();
            return read(imageHz);
        },
        {before.gainRatio, before.phaseSkewRad / kDeg}, {options.ratioStep, options.skewStepDeg}, 1e-5, options.maxEvaluations);
    if (failure) return restore(*failure);
    c.gainRatio = skew.x[0];
    c.phaseSkewRad = skew.x[1] * kDeg;
    QXL_TRY(awg_.setCorrection(port_, c));
    rep.correction = awg_.correction(port_); // as stored (clamped to the schema)
    rep.log.push_back(std::format("amplitude ratio {:.5f}, phase skew {:+.4f} deg after {} readings", c.gainRatio, skew.x[1], skew.evaluations));

    rep.carrierDbm = read(carrierHz);
    rep.loAfterDbc = read(rep.loHz) - rep.carrierDbm;
    rep.imageAfterDbc = read(imageHz) - rep.carrierDbm;
    if (failure) return restore(*failure);
    rep.log.push_back(std::format("after calibration: LO leakage {:.1f} dBc, image {:.1f} dBc", rep.loAfterDbc, rep.imageAfterDbc));
    return rep;
}

} // namespace qlab::instr
