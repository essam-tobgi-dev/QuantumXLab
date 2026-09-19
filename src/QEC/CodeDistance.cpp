// Spec 16 §1, T09 (2.1) — exhaustive code distance: the minimum weight of a Pauli string that
// commutes with every generator and is not in the stabilizer group.
#include "QEC/Code.hpp"
#include "QEC/Detail.hpp"
#include <array>
#include <format>

namespace qlab::qec {
namespace {
using detail::Mask;
using detail::MaskSpan;

enum class Letters { XOnly, ZOnly, All };

struct Found {
    std::uint32_t weight = 0; // 0 = no non-trivial logical operator over these letters
    Mask witness;
};

class DistanceSearch {
  public:
    DistanceSearch(const StabilizerCode& code, std::uint64_t budget) : n_(code.n), budget_(budget) {
        for (const PauliString& g : code.stabilizers)
            gens_.push_back(detail::toMask(g));
        group_ = MaskSpan(gens_);
    }

    // Weights are scanned in increasing order, so the first hit is the minimum.
    Result<Found> minimum(Letters letters) {
        const std::uint64_t limit = std::uint64_t{1} << n_;
        for (std::uint32_t w = 1; w <= n_; ++w) {
            for (std::uint64_t s = (std::uint64_t{1} << w) - 1; s != 0;
                 s = detail::nextSubset(s, limit)) {
                Mask hit;
                QXL_TRY_ASSIGN(const bool found, scanSupport(s, w, letters, hit));
                if (found)
                    return Found{w, hit};
            }
        }
        return Found{};
    }

  private:
    bool isLogical(Mask m) const {
        for (Mask g : gens_)
            if (!detail::commute(m, g))
                return false;
        return !group_.contains(m);
    }

    Result<bool> scanSupport(std::uint64_t support, std::uint32_t w, Letters letters, Mask& hit) {
        if (letters != Letters::All) {
            if (!spend(1))
                return exhausted();
            const Mask m = letters == Letters::XOnly ? Mask{support, 0} : Mask{0, support};
            if (!isLogical(m))
                return false;
            hit = m;
            return true;
        }
        std::array<std::uint64_t, 64> bit{};
        std::uint32_t count = 0;
        for (std::uint64_t rest = support; rest != 0; rest &= rest - 1)
            bit[count++] = rest & (~rest + 1);
        std::array<std::uint8_t, 64> digit{}; // 0 = X, 1 = Y, 2 = Z per support qubit
        for (;;) {
            if (!spend(1))
                return exhausted();
            Mask m;
            for (std::uint32_t i = 0; i < w; ++i) {
                if (digit[i] != 2)
                    m.x |= bit[i];
                if (digit[i] != 0)
                    m.z |= bit[i];
            }
            if (isLogical(m)) {
                hit = m;
                return true;
            }
            std::uint32_t i = 0;
            while (i < w && digit[i] == 2)
                digit[i++] = 0;
            if (i == w)
                return false;
            ++digit[i];
        }
    }

    bool spend(std::uint64_t c) {
        if (budget_ < c)
            return false;
        budget_ -= c;
        return true;
    }
    static std::unexpected<Error> exhausted() {
        return fail(err::TooLarge,
                    "exhaustive distance search exceeds its budget of candidate strings");
    }

    std::uint32_t n_;
    std::uint64_t budget_;
    std::vector<Mask> gens_;
    MaskSpan group_;
};

} // namespace

Result<DistanceReport> computeDistance(const StabilizerCode& code) {
    if (code.n == 0 || code.n > 62)
        return fail(
            err::TooLarge,
            std::format("exhaustive distance search supports 1 ≤ n ≤ 62, code '{}' has n = {}",
                        code.id, code.n));
    for (const PauliString& g : code.stabilizers)
        if (g.n != code.n)
            return fail(err::BadPauli,
                        std::format("code '{}': generator '{}' has length {}, expected {}", code.id,
                                    g.str(), g.n, code.n));
    DistanceSearch search(code, 200'000'000ull);
    DistanceReport report;
    Found best;
    if (code.isCss()) {
        // For a CSS code a minimum-weight logical operator can be taken purely X-type or purely
        // Z-type: if X(a)Z(b) is a non-trivial logical, X(a) and Z(b) are both in the normalizer
        // and at least one of them is outside the stabilizer group (T09 §4.6).
        QXL_TRY_ASSIGN(const Found fx, search.minimum(Letters::XOnly));
        QXL_TRY_ASSIGN(const Found fz, search.minimum(Letters::ZOnly));
        report.dX = fx.weight;
        report.dZ = fz.weight;
        if (fx.weight == 0 && fz.weight == 0)
            return fail(err::BadLogical,
                        std::format("code '{}' has no non-trivial logical operator", code.id));
        best = (fz.weight == 0 || (fx.weight != 0 && fx.weight <= fz.weight)) ? fx : fz;
        report.distance = best.weight;
        bool hasX = false, hasZ = false;
        for (std::uint32_t j = 0; j < code.checkCount(); ++j)
            (code.checkType(j) == CheckType::X ? hasX : hasZ) = true;
        // Checks of a single Pauli type detect only the opposite error type: the shipped figure is
        // the distance against that error (T09 §4.1: "[[3,1,1]] but corrects one bit flip").
        const Found declared = (hasZ && !hasX && fx.weight != 0)   ? fx
                               : (hasX && !hasZ && fz.weight != 0) ? fz
                                                                   : best;
        report.declared = declared.weight;
        report.witness = detail::fromMask(declared.witness, code.n);
        return report;
    }
    QXL_TRY_ASSIGN(best, search.minimum(Letters::All));
    if (best.weight == 0)
        return fail(err::BadLogical,
                    std::format("code '{}' has no non-trivial logical operator", code.id));
    report.distance = report.declared = best.weight;
    report.witness = detail::fromMask(best.witness, code.n);
    return report;
}

} // namespace qlab::qec
