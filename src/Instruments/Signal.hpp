#pragma once
// Spec 12 §7, §11 — signals between instruments and the routing matrix. A node of the signal path
// (AWG output, generator, mixer output, a fridge line at a stage, the digitizer input) delivers its
// signal as a complex envelope z(t) referred to a carrier: v_RF(t) = Re[z(t) e^{+i2π f_ref t}]
// volts into Z0 = 50 Ω (T07 (5.1)); f_ref = 0 for baseband, where Re z = I and Im z = Q. A tone of
// envelope amplitude A carries A²/2Z0 watts. The spectrum analyzer, oscilloscope and power meter
// attach to any node by name; removing an edge (a cable disconnected in the 3D scene) leaves the
// nodes downstream without signal.
#include "Core/Json.hpp"
#include "Instruments/Types.hpp"
#include "Numerics/Types.hpp"
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace qlab::instr {

using num::Complex;

struct SignalRequest {
    std::size_t samples =
        0; // 0: `durationS` decides, else the whole schedule (4096 for a CW source)
    double durationS = 0.0;    // record length in seconds when `samples` is 0
    double sampleRateHz = 0.0; // 0: the source's own rate; a CW source follows the requested rate
    double t0S = 0.0;          // record start inside the schedule (ignored when `centered`)
    bool centered = false; // centre the record on the middle of the schedule (spectrum analyzer)
    bool envelopeView =
        false; // ideal schedule envelope: no IF, predistortion or quantisation (class Exact)
    std::uint64_t noiseSeed = 0;
};

struct Signal {
    std::string node;
    double referenceHz = 0.0; // carrier of the complex envelope, 0 = baseband
    double sampleRateHz = 0.0;
    double t0S = 0.0;             // time of samples[0]
    std::vector<Complex> samples; // volts (peak)
    double noisePsdWPerHz = 0.0;  // one-sided thermal noise density at the node, k_B T
    double fullScaleV =
        0.0; // reference level of "dBc" quantities (AWG full scale through the chain)
    FidelityClass cls = FidelityClass::Model;
    bool connected = true; // false: an upstream cable is disconnected
    std::string note;
    // Frequencies worth a marker, named by the source: a mixer reports "carrier" (f_LO + f_IF),
    // "lo" and "image" (f_LO − f_IF).
    std::map<std::string, double> landmarks;

    // Number of samples a request asks for, given this source's rate and whole-record length.
    static std::size_t requestedSamples(const SignalRequest& r, double sampleRateHz,
                                        std::size_t whole);
};

class ISignalSource {
  public:
    virtual ~ISignalSource() = default;
    // The record of one output port. `port` selects among several ("ch[2]"); sources with a single
    // port ignore it.
    virtual Result<Signal> signal(std::string_view port, const SignalRequest& request) const = 0;
    // Native sample rate of a port; 0 for a CW source, which follows the requested rate.
    virtual double sampleRateHz(std::string_view port) const = 0;
};

// Scale every sample by a power gain in dB (negative = attenuation).
void scaleSignal(Signal& s, double gainDb);
// Mean power of the record in watts, ⟨|z|²⟩ / 2Z0.
double meanPowerWatts(const Signal& s);

// ---- routing matrix (spec 12 §11) ----------------------------------------------------------
// Nodes are named "<instrument>.<port>" for instrument outputs ("awg[0].ch[0]", "sg_mw[0].rf",
// "iq_mixer[0].rf") and "line.<lineId>.<STAGE>" for a fridge line at a stage. An edge carries the
// signal of `from` to `to` with a gain; `connected = false` is a removed cable.
struct RouteEdge {
    std::string id; // "awg[0].ch[0]->iq_mixer[0].if"
    std::string from, to;
    double gainDb = 0.0;
    bool connected = true;
};

class SignalGraph {
  public:
    void addNode(std::string name, const ISignalSource* source = nullptr, std::string port = {});
    // Adds both endpoints when missing. The edge id defaults to "from->to".
    void addEdge(std::string from, std::string to, double gainDb = 0.0, std::string id = {});
    bool hasNode(std::string_view name) const;
    std::vector<std::string> nodes() const;
    std::vector<RouteEdge> edges() const;
    // Cable action of the 3D scene (any thread). Unknown id → BadRouting.
    Result<void> setConnected(std::string_view edgeId, bool connected);
    // Edge feeding `node`, if any. A node has at most one input; a two-input device (the IQ mixer)
    // is a source whose input ports "….if" and "….lo" are nodes of their own.
    std::optional<RouteEdge> inputOf(std::string_view node) const;

    // The signal at `node`: walks upstream to the nearest node with a source, applies the edge
    // gains on the way back. A disconnected edge yields a record of zeros with `connected = false`
    // at the source's sample rate. Errors: BadRouting (unknown node, no source upstream, a cycle).
    Result<Signal> signalAt(std::string_view node, const SignalRequest& request) const;
    // Native sample rate of the source feeding `node` (0: a CW source).
    Result<double> sampleRateAt(std::string_view node) const;

    core::Json toJson() const;
    // Restores edges (ids, endpoints, gains, connection state); sources are re-attached by the
    // registry. Unknown fields are ignored.
    static Result<SignalGraph> fromJson(const core::Json& j);

  private:
    struct Node {
        const ISignalSource* source = nullptr;
        std::string port;
    };
    struct Path { // from `node` up to the nearest source
        const ISignalSource* source = nullptr;
        std::string port, sourceNode;
        double gainDb = 0.0;
        bool connected = true;
    };
    Result<Path> resolve(std::string_view node) const;
    std::map<std::string, Node, std::less<>> nodes_;
    std::vector<RouteEdge> edges_;
    std::unique_ptr<std::mutex> mu_ =
        std::make_unique<std::mutex>(); // edges change while workers read
};

} // namespace qlab::instr
