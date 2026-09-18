// Spec 16 §2 — generated code families: `repetition_{bitflip,phaseflip}_d` and the rotated planar
// surface code `surface_rot_d` with the layout, boundaries and CNOT orders of spec 16 §2.1.
#include "QEC/Code.hpp"
#include <format>

namespace qlab::qec {
namespace {

PauliString onSupport(std::uint32_t n, const std::vector<std::uint32_t>& qubits, char letter) {
    PauliString p = PauliString::identity(n);
    for (std::uint32_t q : qubits) p.setLetter(q, letter);
    return p;
}

Status requireOddDistance(std::uint32_t d, std::string_view family) {
    if (d < 3 || d % 2 == 0)
        return fail(err::BadOptions, std::format("{} code needs an odd distance d ≥ 3, got {}", family, d));
    return {};
}

} // namespace

Result<StabilizerCode> makeRepetitionCode(std::uint32_t d, bool phaseFlip) {
    QXL_TRY(requireOddDistance(d, "repetition"));
    StabilizerCode c;
    c.id = std::format("repetition_{}_{}", phaseFlip ? "phaseflip" : "bitflip", d);
    c.n = d;
    c.k = 1;
    c.d = d;
    c.family = CodeFamily::Repetition;
    const char check = phaseFlip ? 'X' : 'Z';
    std::vector<std::uint32_t> all;
    for (std::uint32_t q = 0; q < d; ++q) {
        all.push_back(q);
        c.dataLayout.push_back({double(q), 0.0});
    }
    for (std::uint32_t q = 0; q + 1 < d; ++q) {
        c.stabilizers.push_back(onSupport(d, {q, q + 1}, check));
        c.ancillas.push_back({phaseFlip ? CheckType::X : CheckType::Z, {double(q) + 0.5, 1.0}, {q, q + 1}});
    }
    // T09 §4.1–4.2: the protected logical is the full string, its partner a single letter on qubit 0.
    c.logicalX.push_back(phaseFlip ? onSupport(d, {0}, 'X') : onSupport(d, all, 'X'));
    c.logicalZ.push_back(phaseFlip ? onSupport(d, all, 'Z') : onSupport(d, {0}, 'Z'));
    c.defaultDecoder = d == 3 ? "lookup" : "union_find";
    c.theoryRefs = {phaseFlip ? "T09#4.2-three-qubit-phase-flip-code" : "T09#4.1-three-qubit-bit-flip-code"};
    c.notes = phaseFlip ? "Hadamard-conjugated repetition code; corrects (d-1)/2 Z errors; distance 1 against X."
                        : "Corrects (d-1)/2 X errors; distance 1 against Z.";
    return c;
}

Result<StabilizerCode> makeRotatedSurfaceCode(std::uint32_t d) {
    QXL_TRY(requireOddDistance(d, "rotated surface"));
    if (d > 99) return fail(err::TooLarge, std::format("rotated surface code distance {} exceeds the generator limit 99", d));
    StabilizerCode c;
    c.id = std::format("surface_rot_{}", d);
    c.n = d * d;
    c.k = 1;
    c.d = d;
    c.family = CodeFamily::SurfaceRotated;
    // Data qubit i·d + j sits at (2i + 1, 2j + 1); x grows to the east, y to the north.
    for (std::uint32_t i = 0; i < d; ++i)
        for (std::uint32_t j = 0; j < d; ++j) c.dataLayout.push_back({2.0 * i + 1.0, 2.0 * j + 1.0});
    auto dataAt = [d](int x, int y) -> int {
        if (x < 1 || y < 1 || x > int(2 * d - 1) || y > int(2 * d - 1)) return -1;
        return ((x - 1) / 2) * int(d) + (y - 1) / 2;
    };
    // Ancilla cells (2i, 2j), scanned by x then y: X-type when i + j is even. The bulk keeps every
    // cell; the left/right edges keep their Z cells and the top/bottom edges their X cells, which
    // become the weight-2 boundary checks (spec 16 §2.1). Corners hold nothing.
    for (std::uint32_t i = 0; i <= d; ++i)
        for (std::uint32_t j = 0; j <= d; ++j) {
            const bool xType = (i + j) % 2 == 0;
            const bool edgeX = i == 0 || i == d, edgeY = j == 0 || j == d;
            if (edgeX && edgeY) continue;
            if (edgeX && xType) continue;    // left/right edges: Z checks only
            if (edgeY && !xType) continue;   // top/bottom edges: X checks only
            const int ax = int(2 * i), ay = int(2 * j);
            const int nw = dataAt(ax - 1, ay + 1), ne = dataAt(ax + 1, ay + 1);
            const int sw = dataAt(ax - 1, ay - 1), se = dataAt(ax + 1, ay - 1);
            AncillaSpec a;
            a.type = xType ? CheckType::X : CheckType::Z;
            a.coord = {double(ax), double(ay)};
            // Tomita–Svore: X checks NW, NE, SW, SE; Z checks NW, SW, NE, SE (T09 §5.2).
            for (int q : xType ? std::vector<int>{nw, ne, sw, se} : std::vector<int>{nw, sw, ne, se})
                if (q >= 0) a.order.push_back(static_cast<std::uint32_t>(q));
            c.stabilizers.push_back(onSupport(c.n, a.order, xType ? 'X' : 'Z'));
            c.ancillas.push_back(std::move(a));
        }
    // X̄: the left column (x = 1), joining the top and bottom X boundaries. Z̄: the top row
    // (y = 2d − 1), joining the left and right Z boundaries.
    std::vector<std::uint32_t> column, row;
    for (std::uint32_t t = 0; t < d; ++t) {
        column.push_back(t);
        row.push_back(t * d + (d - 1));
    }
    c.logicalX.push_back(onSupport(c.n, column, 'X'));
    c.logicalZ.push_back(onSupport(c.n, row, 'Z'));
    c.defaultDecoder = d == 3 ? "lookup" : "union_find";
    c.theoryRefs = {"T09#5.1-rotated-planar-layout", "T09#5.2-syndrome-extraction-circuit"};
    c.notes = "Rotated planar layout; X checks on top/bottom boundaries, Z checks on left/right; CNOT order X: "
              "NW,NE,SW,SE; Z: NW,SW,NE,SE (Tomita–Svore).";
    return c;
}

} // namespace qlab::qec
