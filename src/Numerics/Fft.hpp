#pragma once
// Spec 06 §9 — FFT (radix-2 in place, Bluestein for arbitrary N) and windows with ENBW.
#include "Numerics/Types.hpp"
#include <span>
#include <string_view>
namespace qlab::num {

// Forward unnormalised: X_k = Σ x_n e^{-2πi kn/N}; inverse divides by N.
void fftInPlace(std::span<Complex> x, bool inverse = false); // any N (radix-2 or Bluestein)
Vector fft(std::span<const Complex> x, bool inverse = false);
Vector rfft(std::span<const double> x);               // full complex spectrum of a real signal
void fftShift(std::span<Complex> x);                  // move DC to centre
RealVector fftFreq(std::size_t n, double sampleRate); // bin frequencies (unshifted order)
bool isPowerOfTwo(std::size_t n);

enum class Window { Rect, Hann, Hamming, Blackman, FlatTop };
RealVector window(Window w, std::size_t n);
double windowEnbw(Window w);         // equivalent noise bandwidth in bins
double windowCoherentGain(Window w); // mean of the window (amplitude correction)
std::string_view windowName(Window w);

// Power spectrum in dB relative to full scale of a windowed signal; returns (freq, dB) with
// `sampleRate`.
struct Spectrum {
    RealVector freqHz;
    RealVector powerDb;
    double enbwHz;
};
Spectrum powerSpectrum(std::span<const Complex> x, double sampleRate, Window w = Window::Hann);

} // namespace qlab::num
