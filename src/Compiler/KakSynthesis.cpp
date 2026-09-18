// Spec 14 §5.5, T02 §6 — circuits for the canonical gate N(a,b,c) = exp(i(a·XX + b·YY + c·ZZ)) with
// the minimal cx count, and the block resynthesis pass. With control q0 and target q1 (time order,
// each identity checked numerically by tests/Compiler):
//   3 cx: rz(−π/2) q0; cx q0,q1; rz(π/2 − 2c) q1; ry(2a − π/2) q0; cx q1,q0; ry(π/2 − 2b) q0;
//         cx q0,q1; rz(π/2) q1                                            (= e^{−iπ/4} N)
//   2 cx: N(a,0,c) = cx q0,q1; rx(−2a) q0; rz(−2c) q1; cx q0,q1; a zero XX (ZZ) coordinate is
//         moved to the YY slot by conjugation with S⊗S (Rx(π/2)⊗Rx(π/2))
//   1 cx: N(±π/4,0,0) = rxx(∓π/2), the Mølmer–Sørensen form of cx (T02 §2.3)
#include "Compiler/CircuitUtil.hpp"
#include "Compiler/Decompose.hpp"
#include "Compiler/Kak.hpp"
#include "Numerics/Matrix.hpp"
#include "Numerics/Tensor.hpp"
#include <cmath>
#include <numbers>

namespace qlab::compiler {
namespace {
constexpr double kPi = std::numbers::pi;
constexpr double kZero = 1e-9;

// Builds the {U, cx} sequence, multiplying neighbouring single-qubit factors into one U per wire.
class Emitter {
public:
    Emitter(ir::Wire q0, ir::Wire q1, const SourceSpan& span) : wires_{q0, q1}, span_(span) {
        pending_[0] = pending_[1] = num::Matrix::identity(2);
    }
    void one(int q, const num::Matrix& m) { pending_[q] = num::matmul(m, pending_[q]); }
    void one(int q, std::string_view name, std::vector<double> params = {}) { one(q, *ir::gates::matrix(name, params)); }
    Status cx(int control, int target) {
        QXL_TRY(flush(0));
        QXL_TRY(flush(1));
        QXL_TRY_ASSIGN(ir::Gate g, gate("cx", {wires_[control], wires_[target]}, {}, span_));
        out_.push_back(std::move(g));
        return {};
    }
    Result<std::vector<ir::Gate>> finish() {
        QXL_TRY(flush(0));
        QXL_TRY(flush(1));
        return std::move(out_);
    }

private:
    Status flush(int q) {
        const EulerAngles e = eulerAngles(pending_[q].view());
        pending_[q] = num::Matrix::identity(2);
        if (e.theta < kAngleEps && std::abs(wrapAngle(e.phi + e.lambda)) < kAngleEps) return {};   // identity up to phase
        QXL_TRY_ASSIGN(ir::Gate g, gate("U", {wires_[q]}, {e.theta, wrapAngle(e.phi), wrapAngle(e.lambda)}, span_));
        out_.push_back(std::move(g));
        return {};
    }
    ir::Wire wires_[2];
    SourceSpan span_;
    num::Matrix pending_[2];
    std::vector<ir::Gate> out_;
};

// N(a, 0, c) with two cx.
Status twoCx(Emitter& e, double a, double c) {
    QXL_TRY(e.cx(0, 1));
    e.one(0, "rx", {-2 * a});
    e.one(1, "rz", {-2 * c});
    return e.cx(0, 1);
}

Status canonical(Emitter& e, const KakDecomposition& k) {
    const bool za = std::abs(k.a) < kZero, zb = std::abs(k.b) < kZero;
    const std::uint32_t count = k.cxCount(kZero);
    if (count == 0) return {};
    if (count == 1) {   // bring the ±π/4 coordinate to XX: S⊗S maps XX → YY, H⊗H maps XX → ZZ
        const double v = !za ? k.a : !zb ? k.b : k.c;
        const char* into = !za ? nullptr : !zb ? "sdg" : "h";
        const char* back = !za ? nullptr : !zb ? "s" : "h";
        if (into) { e.one(0, into); e.one(1, into); }
        if (v > 0) {   // rxx(−π/2)
            e.one(0, "rx", {-kPi / 2}); e.one(1, "rx", {-kPi / 2}); e.one(0, "ry", {-kPi / 2});
            QXL_TRY(e.cx(0, 1));
            e.one(0, "ry", {kPi / 2});
        } else {       // rxx(+π/2)
            e.one(0, "ry", {-kPi / 2});
            QXL_TRY(e.cx(0, 1));
            e.one(0, "ry", {kPi / 2}); e.one(1, "rx", {kPi / 2}); e.one(0, "rx", {kPi / 2});
        }
        if (back) { e.one(0, back); e.one(1, back); }
        return {};
    }
    if (count == 2) {
        if (zb) return twoCx(e, k.a, k.c);
        if (za) {   // N(0,b,c) = (S⊗S) N(b,0,c) (S†⊗S†)
            e.one(0, "sdg"); e.one(1, "sdg");
            QXL_TRY(twoCx(e, k.b, k.c));
            e.one(0, "s"); e.one(1, "s");
            return {};
        }
        // N(a,b,0) = (W⊗W) N(a,0,b) (W†⊗W†), W = Rx(π/2): YY ↔ ZZ
        e.one(0, "rx", {-kPi / 2}); e.one(1, "rx", {-kPi / 2});
        QXL_TRY(twoCx(e, k.a, k.b));
        e.one(0, "rx", {kPi / 2}); e.one(1, "rx", {kPi / 2});
        return {};
    }
    e.one(0, "rz", {-kPi / 2});
    QXL_TRY(e.cx(0, 1));
    e.one(1, "rz", {kPi / 2 - 2 * k.c});
    e.one(0, "ry", {2 * k.a - kPi / 2});
    QXL_TRY(e.cx(1, 0));
    e.one(0, "ry", {kPi / 2 - 2 * k.b});
    QXL_TRY(e.cx(0, 1));
    e.one(1, "rz", {kPi / 2});
    return {};
}
} // namespace

Result<std::vector<ir::Gate>> synthesizeTwoQubit(num::ConstMatrixView u, ir::Wire q0, ir::Wire q1, const SourceSpan& span) {
    QXL_TRY_ASSIGN(const KakDecomposition k, kakDecompose(u));
    Emitter e(q0, q1, span);
    e.one(0, k.before0);
    e.one(1, k.before1);
    QXL_TRY(canonical(e, k));
    e.one(0, k.after0);
    e.one(1, k.after1);
    return e.finish();
}

} // namespace qlab::compiler
