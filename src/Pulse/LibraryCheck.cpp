// Spec 10 §6 — "Every native gate on every qubit/edge MUST resolve to a defcal, else the device
// fails to load." Collects every gap so the error lists them all.
#include "Pulse/Errors.hpp"
#include "Pulse/Library.hpp"
#include <format>

namespace qlab::pulse {

Result<void> PulseLibrary::checkComplete() const {
    std::vector<std::string> gaps;
    const auto n = static_cast<std::uint32_t>(device_.qubitCount());
    for (auto const& [key, d] : defcals_) {
        (void)d;
        for (auto q : key.qubits)
            if (q >= n)
                gaps.push_back(std::format(
                    "defcal {} names qubit {}, which the device does not have", key.toString(), q));
    }

    const hw::NativeGateSet& g = device_.gates;
    const auto data = device_.dataQubits();
    for (auto q : data) {
        const std::uint32_t one[1] = {q};
        for (auto const& name : g.single)
            if (!has(name, one))
                gaps.push_back(std::format("{}({})", name, q));
        if (!has("measure", one))
            gaps.push_back(std::format("measure({})", q));
        if (!has("reset", one))
            gaps.push_back(std::format("reset({})", q));
    }

    const auto needPair = [&](std::uint32_t a, std::uint32_t b, bool directed) {
        const std::uint32_t ab[2] = {a, b};
        const std::uint32_t ba[2] = {b, a};
        for (auto const& name : g.two)
            if (!has(name, ab) && (directed || !has(name, ba)))
                gaps.push_back(std::format("{}({},{})", name, a, b));
    };
    if (device_.allToAll) {
        for (std::size_t i = 0; i < data.size(); ++i)
            for (std::size_t j = i + 1; j < data.size(); ++j)
                needPair(data[i], data[j], false);
    } else {
        for (auto const& e : device_.edges)
            needPair(e.a, e.b, e.directed);
    }

    if (gaps.empty())
        return {};
    Error err(kErrNoDefcal,
              std::format("device '{}' fails to load: {} native gate invocation(s) have no defcal "
                          "(spec 10 §6)",
                          deviceId_, gaps.size()));
    err.withId("E_NO_DEFCAL");
    for (auto& gap : gaps)
        err.withNote(std::move(gap));
    return fail(std::move(err));
}

} // namespace qlab::pulse
