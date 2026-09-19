#pragma once
// Private to the digitizer's translation units: everything precomputed for one demodulator
// configuration (spec 12 §5).
#include "Instruments/Digitizer.hpp"

namespace qlab::instr {

struct Digitizer::Synth {
    ReadoutParams params;
    int states = 2;
    double sampleRateHz = 1e9;
    double ifHz = 100e6;
    int bits = 12;
    double rangeV = 0.5; // ADC input range ±rangeV
    double lsbV = 0.0;
    std::size_t averages = 1;
    CavityResponse response;      // α_s on the acquisition grid
    std::vector<Complex> weights; // w_k
    std::vector<Complex> demod;   // (2/N) w_k e^{−i2π f_IF t_k}
    std::vector<std::vector<double>>
        meanRecord;              // [state][k]: Re[Ṽ_s e^{i2π f_IF t_k}] volts at the ADC
    std::vector<Complex> meanIq; // noise-free demodulated point of each state
    double voltsPerRootPhoton = 0.0;
    double noiseRmsV = 0.0; // per ADC sample, before quantisation
    double sigmaIq = 0.0;   // per quadrature of one shot, quantisation noise included
    double snrExpected = 0.0, snrTheory = 0.0;

    // ADC record of a cavity field: carrier at f_IF, additive noise, N-bit quantisation with
    // clipping.
    std::vector<double> record(std::span<const Complex> field, core::Random& rng) const;
    std::vector<double> meanOf(std::span<const Complex> field) const;
    Complex demodulate(std::span<const double> record) const;
};

} // namespace qlab::instr
