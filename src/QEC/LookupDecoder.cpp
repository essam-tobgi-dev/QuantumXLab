// Spec 16 §5, T09 §6.1 — lookup-table decoder: errors are enumerated by increasing weight and the
// first one reaching a syndrome is stored, so every entry is a minimum-weight correction.
#include "QEC/Decoder.hpp"
#include <array>
#include <bit>
#include <format>

namespace qlab::qec {
namespace {
using detail::Mask;

enum class Letters { XOnly, ZOnly, All };

struct Filler {
    std::uint32_t n;
    std::vector<Mask> generators; // the table's generators, index bit i ↔ generators[i]
    std::vector<Mask> entries;
    std::vector<std::uint8_t> known;
    std::size_t remaining;
    std::uint64_t budget = 100'000'000ull;
    std::uint32_t maxWeight = 0;

    std::size_t indexOf(Mask error) const {
        std::size_t index = 0;
        for (std::size_t i = 0; i < generators.size(); ++i)
            if (!detail::commute(error, generators[i]))
                index |= std::size_t{1} << i;
        return index;
    }
    void offer(Mask error, std::uint32_t weight) {
        const std::size_t index = indexOf(error);
        if (known[index])
            return;
        known[index] = 1;
        entries[index] = error;
        maxWeight = std::max(maxWeight, weight);
        --remaining;
    }
    // All strings of weight w on the given support subset.
    void offerSupport(std::uint64_t support, std::uint32_t w, Letters letters) {
        if (letters == Letters::XOnly) {
            offer({support, 0}, w);
            return;
        }
        if (letters == Letters::ZOnly) {
            offer({0, support}, w);
            return;
        }
        std::array<std::uint64_t, 64> bit{};
        std::uint32_t count = 0;
        for (std::uint64_t rest = support; rest != 0; rest &= rest - 1)
            bit[count++] = rest & (~rest + 1);
        std::array<std::uint8_t, 64> digit{}; // 0 = X, 1 = Y, 2 = Z
        for (;;) {
            Mask m;
            for (std::uint32_t i = 0; i < w; ++i) {
                if (digit[i] != 2)
                    m.x |= bit[i];
                if (digit[i] != 0)
                    m.z |= bit[i];
            }
            offer(m, w);
            std::uint32_t i = 0;
            while (i < w && digit[i] == 2)
                digit[i++] = 0;
            if (i == w)
                return;
            ++digit[i];
        }
    }
    Status fill(Letters letters) {
        entries.assign(std::size_t{1} << generators.size(), Mask{});
        known.assign(entries.size(), 0);
        remaining = entries.size() - 1;
        known[0] = 1; // trivial syndrome: no correction
        const std::uint64_t limit = std::uint64_t{1} << n;
        for (std::uint32_t w = 1; w <= n && remaining > 0; ++w)
            for (std::uint64_t s = (std::uint64_t{1} << w) - 1; s != 0 && remaining > 0;
                 s = detail::nextSubset(s, limit)) {
                std::uint64_t cost = 1; // 3^w letter assignments per support
                for (std::uint32_t i = 0; letters == Letters::All && i < std::min(w, 39u); ++i)
                    cost *= 3;
                if (budget < cost)
                    return fail(err::TooLarge, "lookup-table build exceeds its enumeration budget");
                budget -= cost;
                offerSupport(s, w, letters);
            }
        // Syndromes no error over these letters can produce stay at "no correction" (they cannot
        // occur for independent generators; kept for robustness).
        return {};
    }
};

} // namespace

SyndromeLattice SyndromeLattice::fromSyndrome(std::span<const std::uint8_t> syndrome) {
    return {std::vector<std::uint8_t>(syndrome.begin(), syndrome.end())};
}

SyndromeLattice SyndromeLattice::fromRecord(const MemoryExperiment& experiment,
                                            std::span<const std::uint8_t> bits) {
    return {experiment.detectionEvents(bits)};
}

SyndromeLattice SyndromeLattice::foldLayers(const MemoryExperiment& experiment,
                                            std::span<const std::uint8_t> events) {
    SyndromeLattice folded{std::vector<std::uint8_t>(experiment.nAncilla, 0)};
    for (std::size_t i = 0; i < experiment.detectors.size() && i < events.size(); ++i)
        folded.events[experiment.detectors[i].check] ^= static_cast<std::uint8_t>(events[i] & 1u);
    return folded;
}

Result<LookupDecoder> LookupDecoder::create(const StabilizerCode& code) {
    if (code.n == 0 || code.n > 62)
        return fail(err::TooLarge,
                    std::format("lookup decoder supports 1 ≤ n ≤ 62, code '{}' has n = {}", code.id,
                                code.n));
    LookupDecoder d;
    d.n_ = code.n;
    d.checks_ = code.checkCount();
    // CSS: the Z checks see only the X component of an error and vice versa (T09 §4.6).
    struct Plan {
        CheckType type;
        Letters letters;
    };
    const std::vector<Plan> plans =
        code.isCss()
            ? std::vector<Plan>{{CheckType::Z, Letters::XOnly}, {CheckType::X, Letters::ZOnly}}
            : std::vector<Plan>{{CheckType::Mixed, Letters::All}};
    for (const Plan& plan : plans) {
        Table table;
        Filler filler{code.n, {}, {}, {}, 0};
        for (std::uint32_t j = 0; j < code.checkCount(); ++j)
            if (!code.isCss() || code.checkType(j) == plan.type) {
                table.generators.push_back(j);
                filler.generators.push_back(detail::toMask(code.stabilizers[j]));
            }
        if (table.generators.empty())
            continue;
        if (table.generators.size() > 20)
            return fail(
                err::TooLarge,
                std::format("code '{}': a lookup table over {} generators needs 2^{} entries",
                            code.id, table.generators.size(), table.generators.size()));
        QXL_TRY(filler.fill(plan.letters));
        table.entries = std::move(filler.entries);
        d.maxWeight_ = std::max(d.maxWeight_, filler.maxWeight);
        d.tables_.push_back(std::move(table));
    }
    return d;
}

std::size_t LookupDecoder::tableEntries() const {
    std::size_t total = 0;
    for (const Table& t : tables_)
        total += t.entries.size();
    return total;
}

Result<Correction> LookupDecoder::decode(const SyndromeLattice& lattice) {
    if (lattice.events.size() != checks_)
        return fail(
            err::BadSyndrome,
            std::format("lookup decoder expects one event per generator ({}), got {}; fold a "
                        "multi-layer lattice with SyndromeLattice::foldLayers",
                        checks_, lattice.events.size()));
    Mask total;
    for (const Table& t : tables_) {
        std::size_t index = 0;
        for (std::size_t i = 0; i < t.generators.size(); ++i)
            if (lattice.events[t.generators[i]] & 1u)
                index |= std::size_t{1} << i;
        total = total ^ t.entries[index];
    }
    Correction c;
    c.pauli = detail::fromMask(total, n_);
    return c;
}

} // namespace qlab::qec
