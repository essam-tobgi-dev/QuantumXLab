#include "Numerics/Fft.hpp"
#include <bit>
#include <cmath>
#include <numbers>
namespace qlab::num {
namespace {
void radix2(std::span<Complex> x, bool inverse) {
    std::size_t n = x.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) { // bit reversal
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(x[i], x[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        double ang = 2 * std::numbers::pi / static_cast<double>(len) * (inverse ? 1.0 : -1.0);
        Complex wl(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) {
            Complex w(1, 0);
            for (std::size_t k = 0; k < len / 2; ++k) {
                Complex u = x[i + k], v = x[i + k + len / 2] * w;
                x[i + k] = u + v;
                x[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse)
        for (auto& v : x)
            v /= static_cast<double>(n);
}
void bluestein(std::span<Complex> x, bool inverse) {
    std::size_t n = x.size();
    std::size_t m = std::bit_ceil(2 * n - 1);
    double sgn = inverse ? 1.0 : -1.0;
    Vector chirp(n);
    for (std::size_t k = 0; k < n; ++k) {
        double ang = sgn * std::numbers::pi * static_cast<double>((k * k) % (2 * n)) /
                     static_cast<double>(n);
        chirp[k] = Complex(std::cos(ang), std::sin(ang));
    }
    Vector a(m, Complex{}), b(m, Complex{});
    for (std::size_t k = 0; k < n; ++k)
        a[k] = x[k] * chirp[k];
    b[0] = std::conj(chirp[0]);
    for (std::size_t k = 1; k < n; ++k)
        b[k] = b[m - k] = std::conj(chirp[k]);
    radix2(a, false);
    radix2(b, false);
    for (std::size_t k = 0; k < m; ++k)
        a[k] *= b[k];
    radix2(a, true);
    for (std::size_t k = 0; k < n; ++k)
        x[k] = a[k] * chirp[k];
    if (inverse)
        for (auto& v : x)
            v /= static_cast<double>(n);
}
} // namespace
bool isPowerOfTwo(std::size_t n) {
    return n && !(n & (n - 1));
}
void fftInPlace(std::span<Complex> x, bool inverse) {
    if (x.size() <= 1)
        return;
    if (isPowerOfTwo(x.size()))
        radix2(x, inverse);
    else
        bluestein(x, inverse);
}
Vector fft(std::span<const Complex> x, bool inverse) {
    Vector y(x.begin(), x.end());
    fftInPlace(y, inverse);
    return y;
}
Vector rfft(std::span<const double> x) {
    Vector y(x.size());
    for (std::size_t i = 0; i < x.size(); ++i)
        y[i] = x[i];
    fftInPlace(y);
    return y;
}
void fftShift(std::span<Complex> x) {
    std::size_t n = x.size(), h = n / 2;
    Vector tmp(x.begin(), x.end());
    for (std::size_t i = 0; i < n; ++i)
        x[(i + h) % n] = tmp[i];
}
RealVector fftFreq(std::size_t n, double sampleRate) {
    RealVector f(n);
    for (std::size_t k = 0; k < n; ++k) {
        double kk = k < (n + 1) / 2 ? static_cast<double>(k)
                                    : static_cast<double>(k) - static_cast<double>(n);
        f[k] = kk * sampleRate / static_cast<double>(n);
    }
    return f;
}
RealVector window(Window w, std::size_t n) {
    RealVector v(n, 1.0);
    if (n < 2)
        return v;
    double N = static_cast<double>(n - 1);
    for (std::size_t i = 0; i < n; ++i) {
        double t = 2 * std::numbers::pi * static_cast<double>(i) / N;
        switch (w) {
        case Window::Rect:
            v[i] = 1.0;
            break;
        case Window::Hann:
            v[i] = 0.5 - 0.5 * std::cos(t);
            break;
        case Window::Hamming:
            v[i] = 0.54 - 0.46 * std::cos(t);
            break;
        case Window::Blackman:
            v[i] = 0.42 - 0.5 * std::cos(t) + 0.08 * std::cos(2 * t);
            break;
        case Window::FlatTop:
            v[i] = 0.21557895 - 0.41663158 * std::cos(t) + 0.277263158 * std::cos(2 * t) -
                   0.083578947 * std::cos(3 * t) + 0.006947368 * std::cos(4 * t);
            break;
        }
    }
    return v;
}
double windowEnbw(Window w) {
    switch (w) {
    case Window::Rect:
        return 1.0;
    case Window::Hann:
        return 1.5;
    case Window::Hamming:
        return 1.3628;
    case Window::Blackman:
        return 1.7268;
    case Window::FlatTop:
        return 3.7702;
    }
    return 1.0;
}
double windowCoherentGain(Window w) {
    switch (w) {
    case Window::Rect:
        return 1.0;
    case Window::Hann:
        return 0.5;
    case Window::Hamming:
        return 0.54;
    case Window::Blackman:
        return 0.42;
    case Window::FlatTop:
        return 0.21557895;
    }
    return 1.0;
}
std::string_view windowName(Window w) {
    switch (w) {
    case Window::Rect:
        return "rect";
    case Window::Hann:
        return "hann";
    case Window::Hamming:
        return "hamming";
    case Window::Blackman:
        return "blackman";
    case Window::FlatTop:
        return "flattop";
    }
    return "?";
}
Spectrum powerSpectrum(std::span<const Complex> x, double sampleRate, Window w) {
    std::size_t n = x.size();
    RealVector win = window(w, n);
    Vector y(n);
    double cg = windowCoherentGain(w);
    for (std::size_t i = 0; i < n; ++i)
        y[i] = x[i] * win[i] / (cg * static_cast<double>(n));
    fftInPlace(y);
    Spectrum s;
    s.freqHz = fftFreq(n, sampleRate);
    s.powerDb.resize(n);
    for (std::size_t i = 0; i < n; ++i)
        s.powerDb[i] = 10.0 * std::log10(std::max(std::norm(y[i]), 1e-300));
    s.enbwHz = windowEnbw(w) * sampleRate / static_cast<double>(n);
    return s;
}
} // namespace qlab::num
