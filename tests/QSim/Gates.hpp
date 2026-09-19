#pragma once
// Shared gate matrices and circuit helpers for the QSim tests (T02 conventions, little-endian).
#include "QSim/QSim.hpp"
#include <cmath>
#include <numbers>

namespace qtest {
using namespace qlab;
using namespace qlab::qsim;
using num::Complex;
using num::Matrix;

inline Matrix mat2(Complex a, Complex b, Complex c, Complex d) {
    Matrix m(2, 2);
    m(0, 0) = a;
    m(0, 1) = b;
    m(1, 0) = c;
    m(1, 1) = d;
    return m;
}
inline Matrix I2() {
    return mat2(1, 0, 0, 1);
}
inline Matrix X() {
    return mat2(0, 1, 1, 0);
}
inline Matrix Y() {
    return mat2(0, Complex(0, -1), Complex(0, 1), 0);
}
inline Matrix Z() {
    return mat2(1, 0, 0, -1);
}
inline Matrix H() {
    const double s = 1.0 / std::sqrt(2.0);
    return mat2(s, s, s, -s);
}
inline Matrix S() {
    return mat2(1, 0, 0, Complex(0, 1));
}
inline Matrix Sdg() {
    return mat2(1, 0, 0, Complex(0, -1));
}
inline Matrix T() {
    return mat2(1, 0, 0, std::exp(Complex(0, std::numbers::pi / 4)));
}
inline Matrix SX() {
    Matrix m(2, 2);
    m(0, 0) = Complex(0.5, 0.5);
    m(0, 1) = Complex(0.5, -0.5);
    m(1, 0) = Complex(0.5, -0.5);
    m(1, 1) = Complex(0.5, 0.5);
    return m;
}
inline Matrix RZ(double th) {
    return mat2(std::exp(Complex(0, -th / 2)), 0, 0, std::exp(Complex(0, th / 2)));
}
inline Matrix RY(double th) {
    return mat2(std::cos(th / 2), -std::sin(th / 2), std::sin(th / 2), std::cos(th / 2));
}
inline Matrix RX(double th) {
    return mat2(std::cos(th / 2), Complex(0, -std::sin(th / 2)), Complex(0, -std::sin(th / 2)),
                std::cos(th / 2));
}
inline Matrix phase(double lam) {
    return mat2(1, 0, 0, std::exp(Complex(0, lam)));
}

// Two-qubit matrices in the basis |q1 q0> (targets[0] = q0 = least significant).
inline Matrix CX() { // control = targets[0], target = targets[1]
    Matrix m(4, 4);
    m(0, 0) = 1;
    m(1, 3) = 1;
    m(2, 2) = 1;
    m(3, 1) = 1;
    return m;
}
inline Matrix CZ() {
    Matrix m(4, 4);
    m(0, 0) = 1;
    m(1, 1) = 1;
    m(2, 2) = 1;
    m(3, 3) = -1;
    return m;
}
inline Matrix SWAP() {
    Matrix m(4, 4);
    m(0, 0) = 1;
    m(1, 2) = 1;
    m(2, 1) = 1;
    m(3, 3) = 1;
    return m;
}
inline std::vector<QubitIndex> q(std::initializer_list<std::uint32_t> xs) {
    std::vector<QubitIndex> v;
    for (auto x : xs)
        v.push_back(QubitIndex{x});
    return v;
}
} // namespace qtest
