// Spec 14 §4.1 — native gate sets per technology.
#include "Compiler/Target.hpp"
#include "Compiler/Types.hpp"
#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace qlab::compiler {
namespace {
bool contains(const std::vector<std::string>& v, std::string_view s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}
std::string familyOf(std::string_view entangler) {
    if (entangler == "cx" || entangler == "cz" || entangler == "ecr" || entangler == "siswap")
        return std::string(entangler);
    if (entangler == "ms" || entangler == "rxx")
        return "rxx";
    return {};
}
} // namespace

Target Target::universal() {
    return Target{};
}

Result<Target> Target::forDevice(const hw::Device& dev, std::string_view preferred) {
    Target t;
    t.device = &dev;
    t.name = dev.id;
    t.native1q = dev.gates.single;
    t.native2q = dev.gates.two;
    if (dev.gates.hasSingle("sx"))
        t.basis = Basis1q::ZSX;
    else if (dev.gates.hasSingle("ry") && dev.gates.hasSingle("rz"))
        t.basis = Basis1q::ZYZ;
    else
        return fail(
            err::Unsupported,
            std::format("device '{}' has neither an {{rz, sx}} nor an {{rz, ry}} single-qubit set",
                        dev.id));
    if (dev.gates.two.empty())
        return fail(err::Unsupported,
                    std::format("device '{}' has no native two-qubit gate", dev.id));

    t.entangler = dev.gates.two.front();
    if (!preferred.empty()) {
        // `rxx` and `ms` name the same gate; the device's spelling is the one emitted.
        const bool alias = (preferred == "rxx" && dev.gates.hasTwo("ms")) ||
                           (preferred == "ms" && dev.gates.hasTwo("rxx"));
        if (dev.gates.hasTwo(preferred))
            t.entangler = std::string(preferred);
        else if (alias)
            t.entangler = preferred == "rxx" ? "ms" : "rxx";
        else
            return fail(ErrorCode::InvalidArgument,
                        std::format("'{}' is not a native two-qubit gate of device '{}'", preferred,
                                    dev.id));
        t.native2q = {t.entangler}; // an explicit choice: the output uses this entangler only
    }
    t.family = familyOf(t.entangler);
    if (t.family.empty())
        return fail(err::Unsupported,
                    std::format("no decomposition rules target the entangler '{}' of device '{}'",
                                t.entangler, dev.id));
    if (t.family == "rxx")
        t.maxEntanglerAngle = std::numbers::pi / 2.0;
    return t;
}

bool Target::isNative1q(std::string_view g) const {
    return contains(native1q, g);
}
bool Target::isNative2q(std::string_view g) const {
    return contains(native2q, g);
}

bool Target::accepts(const ir::Gate& g) const {
    if (g.opaque)
        return true; // defined by a program defcal (spec 13 §5); PulseLower resolves it
    if (!g.controls.empty() || g.adjoint || g.custom)
        return false;
    if (g.targets.size() == 1)
        return isNative1q(g.name);
    if (g.targets.size() != 2 || !isNative2q(g.name))
        return false;
    if (maxEntanglerAngle > 0.0 && g.params.size() == 1)
        return std::abs(g.params[0]) <= maxEntanglerAngle + kAngleEps;
    return true;
}

bool Target::directionOk(const ir::Gate& g) const {
    if (!device || g.targets.size() != 2 || (g.name != "cx" && g.name != "ecr"))
        return true;
    const std::uint32_t c = g.targets[0].index, t = g.targets[1].index;
    if (!device->edgeIndex(c, t))
        return true; // not coupled at all: Route reports QL4030
    return device->nativeDirection(c, t);
}

} // namespace qlab::compiler
