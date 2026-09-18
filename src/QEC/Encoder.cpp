// Spec 16 §3 — encoder synthesis. The decoding Clifford D is found by Gaussian elimination over the
// rows {g_i, Z̄_l, X̄_l}: gates reduce one row at a time to a single Z (or X) on a fresh qubit, and
// multiplying by an already reduced generator clears its qubit from the other rows. The encoder is
// D†. Signs are tracked through T09 §3.1, so the output is the +1 eigenspace exactly.
#include "QEC/Clifford.hpp"
#include "QEC/Encoder.hpp"
#include <algorithm>
#include <format>

namespace qlab::qec {
namespace {

class Synthesizer {
public:
    explicit Synthesizer(const StabilizerCode& code) : code_(code), used_(code.n, 0) {
        rows_ = code.stabilizers;
        for (std::uint32_t l = 0; l < code.k; ++l) {
            rows_.push_back(code.logicalZ[l]);
            rows_.push_back(code.logicalX[l]);
        }
    }

    Result<Schedule> run() {
        const std::uint32_t m = code_.checkCount();
        for (std::uint32_t i = 0; i < m; ++i) {
            QXL_TRY_ASSIGN(const std::uint32_t q, reduceToZ(i));
            // Every other row commutes with ±Z_q, so it has no X there; a remaining Z is removed by
            // multiplying with the reduced generator (a stabilizer: allowed for every row).
            for (std::uint32_t r = 0; r < rows_.size(); ++r)
                if (r != i && rows_[r].z[q]) rows_[r] = rows_[r] * rows_[i];
            if (rows_[i].phase != 0) apply({OpKind::X, q});
        }
        for (std::uint32_t l = 0; l < code_.k; ++l) {
            const std::uint32_t zRow = m + 2 * l, xRow = zRow + 1;
            QXL_TRY_ASSIGN(const std::uint32_t q, reduceToZ(zRow));
            QXL_TRY(reduceToX(xRow, q));   // with gates that keep Z_q fixed
            if (rows_[zRow].phase != 0) apply({OpKind::X, q});
            if (rows_[xRow].phase != 0) apply({OpKind::Z, q});
            if (q != l) {   // logical input l sits on data qubit l
                apply({OpKind::Swap, q, l});
                std::swap(used_[q], used_[l]);
            }
        }
        // Encoder = D†: reversed, each gate inverted (only s ↔ sdg is not self-inverse).
        Schedule s;
        s.qubits = code_.n;
        for (auto it = ops_.rbegin(); it != ops_.rend(); ++it) {
            Op op = *it;
            if (op.kind == OpKind::S) op.kind = OpKind::Sdg;
            else if (op.kind == OpKind::Sdg) op.kind = OpKind::S;
            s.ops.push_back(op);
        }
        return s;
    }

private:
    void apply(Op op) {
        for (PauliString& r : rows_) detail::conjugate(r, op);
        ops_.push_back(op);
    }

    std::vector<std::uint32_t> freeSupport(std::uint32_t row) const {
        std::vector<std::uint32_t> s;
        for (std::uint32_t q : rows_[row].support())
            if (!used_[q]) s.push_back(q);
        return s;
    }

    // Removes every letter of `row` except the one on `pivot`, whose letter must be X: X_p ⊗ L_t → X_p
    // by cx (L = X), cz (L = Z) or sdg + cx (L = Y). All of these fix Z_p.
    void absorbInto(std::uint32_t row, std::uint32_t pivot) {
        for (std::uint32_t t : freeSupport(row)) {
            if (t == pivot) continue;
            const char letter = rows_[row].letter(t);
            if (letter == 'Y') apply({OpKind::Sdg, t});
            apply({letter == 'Z' ? OpKind::CZ : OpKind::CX, pivot, t});
        }
    }

    Result<std::uint32_t> reduceToZ(std::uint32_t row) {
        const std::vector<std::uint32_t> support = freeSupport(row);
        if (support.empty() || support.size() != rows_[row].weight())
            return fail(ErrorCode::Internal, std::format("code '{}': encoder synthesis lost row {} (is the code valid?)", code_.id, row));
        // Highest qubit first, so that the low qubits stay free for the logical inputs.
        std::uint32_t pivot = kNoIndex;
        for (std::uint32_t q : support)
            if (rows_[row].x[q]) pivot = q;
        if (pivot == kNoIndex) {
            // Pure Z string: cx(t → p) maps Z_t Z_p to Z_p (T09 §1.3).
            pivot = support.back();
            for (std::uint32_t t : support)
                if (t != pivot) apply({OpKind::CX, t, pivot});
        } else {
            if (rows_[row].letter(pivot) == 'Y') apply({OpKind::Sdg, pivot});   // S† Y S = X
            absorbInto(row, pivot);
            apply({OpKind::H, pivot});
        }
        used_[pivot] = 1;
        return pivot;
    }

    Status reduceToX(std::uint32_t row, std::uint32_t pivot) {
        if (!rows_[row].x[pivot])
            return fail(ErrorCode::Internal, std::format("code '{}': logical X and Z do not anticommute on the pivot", code_.id));
        if (rows_[row].letter(pivot) == 'Y') apply({OpKind::Sdg, pivot});
        absorbInto(row, pivot);
        return {};
    }

    const StabilizerCode& code_;
    std::vector<PauliString> rows_;
    std::vector<std::uint8_t> used_;
    std::vector<Op> ops_;
};

} // namespace

Result<PauliString> conjugated(const PauliString& p, std::span<const Op> gates) {
    PauliString out = p;
    for (const Op& op : gates)
        if (!detail::conjugate(out, op))
            return fail(err::NotClifford, std::format("cannot conjugate through '{}' on qubit {}", opName(op.kind), op.a));
    return out;
}

Result<Schedule> planEncoder(const StabilizerCode& code) {
    VerifyOptions quick;
    quick.checkLayout = false;
    quick.checkDistance = false;
    QXL_TRY(verifyCode(code, quick));
    for (const PauliString& g : code.stabilizers)
        if (g.phase != 0) return fail(err::BadOptions, std::format("code '{}': generators with a sign are not supported", code.id));
    QXL_TRY_ASSIGN(Schedule s, Synthesizer(code).run());
    // Check the result against the definition: Z_l → Z̄_l and X_l → X̄_l up to stabilizers, the other
    // Z_q into the stabilizer group, all with sign +.
    for (std::uint32_t q = 0; q < code.n; ++q) {
        QXL_TRY_ASSIGN(PauliString image, conjugated(PauliString::single(code.n, q, 'Z'), s.ops));
        if (q < code.k) image = image * code.logicalZ[q];
        int sign = 0;
        if (!inGroup(code.stabilizers, image, &sign) || sign != 1)
            return fail(ErrorCode::Internal, std::format("code '{}': encoder maps Z_{} outside its target", code.id, q));
    }
    for (std::uint32_t l = 0; l < code.k; ++l) {
        QXL_TRY_ASSIGN(PauliString image, conjugated(PauliString::single(code.n, l, 'X'), s.ops));
        int sign = 0;
        if (!inGroup(code.stabilizers, image * code.logicalX[l], &sign) || sign != 1)
            return fail(ErrorCode::Internal, std::format("code '{}': encoder maps X_{} outside its target", code.id, l));
    }
    return s;
}

Result<ir::Circuit> buildEncoder(const StabilizerCode& code) {
    QXL_TRY_ASSIGN(const Schedule s, planEncoder(code));
    ir::Circuit c;
    c.setQubitCount(code.n);
    c.addQubitRegister(ir::QubitRegister{"data", 0, code.n, false});
    for (const Op& op : s.ops) {
        std::vector<ir::Wire> wires = {ir::Wire{op.a}};
        if (op.twoQubit()) wires.push_back(ir::Wire{op.b});
        QXL_TRY_ASSIGN(ir::Gate g, ir::makeGate(opName(op.kind), std::move(wires)));
        c.add(std::move(g));
    }
    core::Json inputs = core::Json::array();
    for (std::uint32_t l = 0; l < code.k; ++l) inputs.push_back(l);
    c.meta()["qec"] = {{"code", code.id}, {"role", "encoder"}, {"input_qubits", std::move(inputs)}};
    return c;
}

} // namespace qlab::qec
