// Spec 15 §4 — expectation values from the shot memory, with the exact value from the state where
// the backend holds the whole distribution, and the summary of the `output` variables.
#include "Data/Fidelity.hpp"
#include "Runtime/Run.hpp"
#include <cmath>
#include <format>
#include <map>

namespace qlab::runtime {
namespace {

// ⟨P⟩ = (1/N) Σ_s (−1)^{parity_s} over the classical bits of the observable, σ = √((1 − ⟨P⟩²)/N).
Expectation fromParity(std::string name, std::span<const ShotRecord> memory,
                       std::span<const std::uint32_t> bits) {
    Expectation e;
    e.observable = std::move(name);
    if (memory.empty())
        return e;
    double sum = 0.0;
    for (const ShotRecord& r : memory) {
        int parity = 0;
        for (std::uint32_t b : bits)
            if (b < r.bits.size())
                parity ^= r.bits[b] & 1;
        sum += parity ? -1.0 : 1.0;
    }
    const double n = static_cast<double>(memory.size());
    e.value = sum / n;
    e.stderr_ = std::sqrt(std::max(0.0, 1.0 - e.value * e.value) / n);
    e.cls = data::FidelityClass::Statistical;
    return e;
}

// The same observable read off the exact Born distribution over the classical bits.
std::optional<double> exactParity(const std::optional<std::vector<double>>& dist,
                                  std::span<const std::uint32_t> bits) {
    if (!dist)
        return std::nullopt;
    double v = 0.0;
    for (std::size_t x = 0; x < dist->size(); ++x) {
        int parity = 0;
        for (std::uint32_t b : bits)
            parity ^= static_cast<int>((x >> b) & 1);
        v += (parity ? -1.0 : 1.0) * (*dist)[x];
    }
    return v;
}
} // namespace

std::vector<Expectation> computeExpectations(const RunResult& r, const ProgramPlan& plan,
                                             const hw::Device* device) {
    std::vector<Expectation> out;
    // Classical bit holding each measured qubit's last outcome.
    std::map<std::uint32_t, std::uint32_t> bitOfQubit; // device qubit → flat bit
    for (const MeasuredBit& m : plan.measurements)
        if (m.bit != ir::kNoBit)
            bitOfQubit[m.physical] = m.bit.index;

    for (const auto& [qubit, bit] : bitOfQubit) {
        const std::uint32_t bits[1] = {bit};
        Expectation e = fromParity(std::format("Z{}", qubit), r.memory, bits);
        e.exact = exactParity(r.exact, bits);
        out.push_back(std::move(e));
    }
    // ⟨Z_i Z_j⟩ on the device edges between measured qubits (spec 15 §1 `computeExpectations`).
    if (device) {
        for (auto a = bitOfQubit.begin(); a != bitOfQubit.end(); ++a)
            for (auto b = std::next(a); b != bitOfQubit.end(); ++b) {
                if (!device->adjacent(a->first, b->first))
                    continue;
                const std::uint32_t bits[2] = {a->second, b->second};
                Expectation e =
                    fromParity(std::format("Z{}Z{}", a->first, b->first), r.memory, bits);
                e.exact = exactParity(r.exact, bits);
                out.push_back(std::move(e));
            }
    }
    // `output` variables: mean and standard error over the shots (spec 15 §4).
    std::size_t k = 0;
    for (const RegisterInfo& reg : r.layout.registers) {
        if (!reg.output)
            continue;
        double sum = 0.0, sum2 = 0.0;
        std::size_t n = 0;
        for (const ShotRecord& s : r.memory) {
            if (k >= s.outputs.size())
                continue;
            sum += s.outputs[k];
            sum2 += s.outputs[k] * s.outputs[k];
            ++n;
        }
        Expectation e;
        e.observable = reg.name;
        if (n > 0) {
            e.value = sum / static_cast<double>(n);
            const double var = std::max(0.0, sum2 / static_cast<double>(n) - e.value * e.value);
            e.stderr_ = std::sqrt(var / static_cast<double>(n));
        }
        e.cls = data::FidelityClass::Statistical;
        out.push_back(std::move(e));
        ++k;
    }
    return out;
}

double RunResult::probability(const std::string& key) const {
    return counts.probability(key);
}

const Expectation* RunResult::expectation(std::string_view name) const {
    for (const Expectation& e : expectations)
        if (e.observable == name)
            return &e;
    return nullptr;
}

std::size_t SweepGrid::points() const {
    std::size_t n = axes.empty() ? 0 : 1;
    for (const SweepAxis& a : axes)
        n *= a.values.size();
    return n;
}

std::vector<double> SweepGrid::at(std::size_t index) const {
    std::vector<double> coords(axes.size(), 0.0);
    for (std::size_t a = axes.size(); a-- > 0;) {
        const std::size_t size = axes[a].values.empty() ? 1 : axes[a].values.size();
        coords[a] = axes[a].values.empty() ? 0.0 : axes[a].values[index % size];
        index /= size;
    }
    return coords;
}

std::vector<std::size_t> SweepResult::shape() const {
    std::vector<std::size_t> s;
    for (const SweepAxis& a : axes)
        s.push_back(a.values.size());
    return s;
}

} // namespace qlab::runtime
