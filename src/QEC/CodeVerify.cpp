// Spec 16 §1 `Verify` — load-time validation of a stabilizer code: commutation, independence,
// logical-operator relations, ancilla annotations (spec 16 §2.1 for the rotated surface code) and
// the declared distance against the exhaustive search.
#include "QEC/Code.hpp"
#include <algorithm>
#include <cmath>
#include <format>

namespace qlab::qec {
namespace {

bool isInteger(double v) { return std::abs(v - std::round(v)) < 1e-9; }
bool isOdd(double v) { return isInteger(v) && (static_cast<long long>(std::llround(v)) & 1LL) != 0; }
bool isEven(double v) { return isInteger(v) && (static_cast<long long>(std::llround(v)) & 1LL) == 0; }

Status verifyShapes(const StabilizerCode& c) {
    if (c.n == 0 || c.k > c.n)
        return fail(err::BadJson, std::format("code '{}': need n ≥ 1 and k ≤ n, got n = {}, k = {}", c.id, c.n, c.k));
    if (c.stabilizers.size() != c.n - c.k)
        return fail(err::NotIndependent, std::format("code '{}': {} generators given, [[{},{}]] needs n − k = {}", c.id,
                                                     c.stabilizers.size(), c.n, c.k, c.n - c.k));
    if (c.logicalX.size() != c.k || c.logicalZ.size() != c.k)
        return fail(err::BadLogical, std::format("code '{}': {} logical X and {} logical Z operators given, k = {}",
                                                 c.id, c.logicalX.size(), c.logicalZ.size(), c.k));
    auto sized = [&](const PauliString& p, std::string_view what, std::size_t j) -> Status {
        if (p.n != c.n)
            return fail(err::BadPauli, std::format("code '{}': {}[{}] = '{}' has length {}, expected n = {}", c.id, what,
                                                   j, p.str(), p.n, c.n));
        if (!p.isHermitian())
            return fail(err::BadPauli, std::format("code '{}': {}[{}] = '{}' is not Hermitian", c.id, what, j, p.str(true)));
        return {};
    };
    for (std::size_t j = 0; j < c.stabilizers.size(); ++j) QXL_TRY(sized(c.stabilizers[j], "stabilizers", j));
    for (std::size_t j = 0; j < c.k; ++j) QXL_TRY(sized(c.logicalX[j], "logical_x", j));
    for (std::size_t j = 0; j < c.k; ++j) QXL_TRY(sized(c.logicalZ[j], "logical_z", j));
    return {};
}

Status verifyGroup(const StabilizerCode& c) {
    for (std::size_t a = 0; a < c.stabilizers.size(); ++a) {
        if (c.stabilizers[a].isIdentity())
            return fail(err::NotIndependent, std::format("code '{}': generator {} is the identity", c.id, a));
        for (std::size_t b = a + 1; b < c.stabilizers.size(); ++b)
            if (!c.stabilizers[a].commutesWith(c.stabilizers[b]))
                return fail(err::NotCommuting, std::format("code '{}': generators {} '{}' and {} '{}' anticommute", c.id,
                                                           a, c.stabilizers[a].str(), b, c.stabilizers[b].str()));
    }
    const std::uint32_t rank = symplecticRank(c.stabilizers);
    if (rank != c.n - c.k)
        return fail(err::NotIndependent, std::format("code '{}': generators have rank {} over F2, expected n − k = {}",
                                                     c.id, rank, c.n - c.k));
    // Independent commuting Hermitian generators cannot multiply to −I (T09 §2.1): the only product
    // with identity letters is the empty one.
    return {};
}

Status verifyLogicals(const StabilizerCode& c) {
    auto normal = [&](const PauliString& l, std::string_view what, std::size_t j) -> Status {
        for (std::size_t g = 0; g < c.stabilizers.size(); ++g)
            if (!l.commutesWith(c.stabilizers[g]))
                return fail(err::BadLogical, std::format("code '{}': {}[{}] = '{}' anticommutes with generator {} '{}'",
                                                         c.id, what, j, l.str(), g, c.stabilizers[g].str()));
        return {};
    };
    for (std::size_t j = 0; j < c.k; ++j) QXL_TRY(normal(c.logicalX[j], "logical_x", j));
    for (std::size_t j = 0; j < c.k; ++j) QXL_TRY(normal(c.logicalZ[j], "logical_z", j));
    for (std::size_t i = 0; i < c.k; ++i)
        for (std::size_t j = 0; j < c.k; ++j) {
            const bool commute = c.logicalX[i].commutesWith(c.logicalZ[j]);
            if (commute == (i == j))
                return fail(err::BadLogical, std::format("code '{}': logical_x[{}] and logical_z[{}] must {}", c.id, i, j,
                                                         i == j ? "anticommute" : "commute"));
            if (i < j && (!c.logicalX[i].commutesWith(c.logicalX[j]) || !c.logicalZ[i].commutesWith(c.logicalZ[j])))
                return fail(err::BadLogical, std::format("code '{}': logical operators {} and {} of one type must commute",
                                                         c.id, i, j));
        }
    return {};
}

Status verifyLayout(const StabilizerCode& c) {
    if (c.dataLayout.size() != c.n)
        return fail(err::BadLayout, std::format("code '{}': layout.data has {} coordinates, expected n = {}", c.id,
                                                c.dataLayout.size(), c.n));
    if (c.ancillas.size() != c.stabilizers.size())
        return fail(err::BadLayout, std::format("code '{}': layout.ancilla has {} entries, expected one per generator ({})",
                                                c.id, c.ancillas.size(), c.stabilizers.size()));
    const bool surface = c.family == CodeFamily::SurfaceRotated;
    if (surface)
        for (std::size_t q = 0; q < c.n; ++q)
            if (!isOdd(c.dataLayout[q].x) || !isOdd(c.dataLayout[q].y))
                return fail(err::BadLayout, std::format("code '{}': data qubit {} must sit at odd coordinates (spec 16 §2.1)",
                                                        c.id, q));
    for (std::uint32_t j = 0; j < c.ancillas.size(); ++j) {
        const AncillaSpec& a = c.ancillas[j];
        const CheckType actual = c.checkType(j);
        if (a.type != CheckType::Mixed && a.type != actual)
            return fail(err::BadLayout, std::format("code '{}': ancilla {} is typed '{}' but generator '{}' is '{}'-type",
                                                    c.id, j, checkTypeName(a.type), c.stabilizers[j].str(),
                                                    checkTypeName(actual)));
        std::vector<std::uint32_t> sorted = a.order;
        std::sort(sorted.begin(), sorted.end());
        if (sorted != c.stabilizers[j].support())
            return fail(err::BadLayout, std::format("code '{}': ancilla {} order does not list the support of generator '{}'",
                                                    c.id, j, c.stabilizers[j].str()));
        if (!surface) continue;
        if (!isEven(a.coord.x) || !isEven(a.coord.y))
            return fail(err::BadLayout, std::format("code '{}': ancilla {} must sit at even coordinates (spec 16 §2.1)", c.id, j));
        const bool evenCell = ((std::llround(a.coord.x) / 2 + std::llround(a.coord.y) / 2) & 1LL) == 0;
        if (actual == CheckType::Mixed || (actual == CheckType::X) != evenCell)
            return fail(err::BadLayout, std::format("code '{}': ancilla {} breaks the checkerboard (X at even i + j)", c.id, j));
        int previous = -1;
        for (std::uint32_t q : a.order) {
            const int layer = tomitaSvoreLayer(actual, a.coord, c.dataLayout[q]);
            if (layer <= previous)
                return fail(err::BadLayout, std::format("code '{}': ancilla {} order is not the Tomita–Svore order "
                                                        "(X: NW,NE,SW,SE; Z: NW,SW,NE,SE) at data qubit {}", c.id, j, q));
            previous = layer;
        }
    }
    return {};
}

} // namespace

CheckType StabilizerCode::checkType(std::uint32_t j) const {
    if (j >= stabilizers.size()) return CheckType::Mixed;
    if (stabilizers[j].isXType()) return CheckType::X;
    return stabilizers[j].isZType() ? CheckType::Z : CheckType::Mixed;
}

bool StabilizerCode::isCss() const {
    for (std::uint32_t j = 0; j < checkCount(); ++j)
        if (checkType(j) == CheckType::Mixed) return false;
    return true;
}

const PauliString& StabilizerCode::logical(LogicalBasis basis, std::uint32_t j) const {
    static const PauliString kEmpty;
    const auto& ops = basis == LogicalBasis::Z ? logicalZ : logicalX;
    return j < ops.size() ? ops[j] : kEmpty;
}

bool StabilizerCode::isMatchable() const {
    // X and Z errors must decode independently: on a non-CSS code a Y error is one fault, not an X
    // edge plus a Z edge, and matching would lose the distance (five-qubit code, T09 §4.5).
    if (!isCss()) return false;
    for (std::uint32_t q = 0; q < n; ++q)
        for (char letter : {'X', 'Z'}) {
            const auto s = syndromeOf(stabilizers, PauliString::single(n, q, letter));
            if (std::count(s.begin(), s.end(), std::uint8_t{1}) > 2) return false;
        }
    return true;
}

int tomitaSvoreLayer(CheckType type, Coord2 ancilla, Coord2 data) {
    const double dx = data.x - ancilla.x, dy = data.y - ancilla.y;
    if (type == CheckType::Mixed || std::abs(std::abs(dx) - 1.0) > 1e-9 || std::abs(std::abs(dy) - 1.0) > 1e-9) return -1;
    const bool west = dx < 0, north = dy > 0;
    // X: NW, NE, SW, SE ("Z" pattern); Z: NW, SW, NE, SE ("N" pattern) — spec 16 §2.1.
    if (type == CheckType::X) return (north ? 0 : 2) + (west ? 0 : 1);
    return (west ? 0 : 2) + (north ? 0 : 1);
}

Status verifyCode(const StabilizerCode& code, const VerifyOptions& options) {
    QXL_TRY(verifyShapes(code));
    QXL_TRY(verifyGroup(code));
    QXL_TRY(verifyLogicals(code));
    if (options.checkLayout) QXL_TRY(verifyLayout(code));
    if (options.checkDistance && code.n <= options.exhaustiveDistanceMaxN) {
        QXL_TRY_ASSIGN(const DistanceReport report, computeDistance(code));
        if (report.declared != code.d)
            return fail(err::BadDistance,
                        std::format("code '{}': declared d = {} but the minimum-weight logical operator '{}' has weight {}",
                                    code.id, code.d, report.witness.str(), report.declared));
    }
    return {};
}

} // namespace qlab::qec
