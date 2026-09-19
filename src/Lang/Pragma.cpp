#include "Lang/Pragma.hpp"
#include <charconv>
#include <cmath>

namespace qlab::lang {

std::size_t Sweep::count() const {
    if (!isRange)
        return values.size();
    if (step == 0 || (to - from) / step < 0)
        return 0;
    return static_cast<std::size_t>(std::floor((to - from) / step + 1e-9)) + 1;
}
std::vector<double> Sweep::grid() const {
    if (!isRange)
        return values;
    std::vector<double> g;
    std::size_t n = count();
    for (std::size_t i = 0; i < n; ++i)
        g.push_back(from + static_cast<double>(i) * step);
    return g;
}

namespace {
bool toDouble(const std::string& s, double& v) {
    char* e = nullptr;
    v = std::strtod(s.c_str(), &e);
    return e && *e == '\0' && !s.empty();
}
bool toU64(const std::string& s, std::uint64_t& v) {
    auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    return r.ec == std::errc{} && r.ptr == s.data() + s.size();
}
std::vector<std::string> splitSet(std::string s) { // "{1, 2, 3}" -> items
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '{' || c == '}' || c == ' ')
            continue;
        if (c == ',') {
            if (!cur.empty())
                out.push_back(cur);
            cur.clear();
        } else
            cur += c;
    }
    if (!cur.empty())
        out.push_back(cur);
    return out;
}
} // namespace

void applyPragma(const PragmaStmt& p, const SourceSpan& span, Pragmas& out,
                 std::vector<Diagnostic>& diags) {
    auto bad = [&](std::string why) {
        diags.push_back(Diagnostics::make("QL2014", span, "qlab." + p.name, why));
    };
    if (!p.isQlab) {
        diags.push_back(Diagnostics::make("QL2051", span, p.name));
        return;
    }
    const auto& a = p.args;
    const std::string& n = p.name;
    if (n == "device") {
        if (a.size() != 1)
            return bad("expected one device id");
        out.device = a[0];
        return;
    }
    if (n == "backend") {
        if (a.size() != 1)
            return bad("expected one backend name");
        const std::string& b = a[0];
        if (b == "auto")
            out.backend = BackendChoice::Auto;
        else if (b == "statevector")
            out.backend = BackendChoice::StateVector;
        else if (b == "densitymatrix")
            out.backend = BackendChoice::DensityMatrix;
        else if (b == "stabilizer")
            out.backend = BackendChoice::Stabilizer;
        else if (b == "lindblad")
            out.backend = BackendChoice::Lindblad;
        else
            return bad("unknown backend '" + b + "'");
        return;
    }
    if (n == "shots") {
        std::uint64_t v;
        if (a.size() != 1 || !toU64(a[0], v) || v < 1 || v > 10000000)
            return bad("shots must be an integer in [1, 1e7]");
        out.shots = v;
        return;
    }
    if (n == "seed") {
        std::uint64_t v;
        if (a.size() != 1 || !toU64(a[0], v))
            return bad("seed must be an unsigned integer");
        out.seed = v;
        return;
    }
    if (n == "noise") {
        if (a.size() != 1)
            return bad("expected ideal | calibrated | custom:<file>");
        if (a[0] == "ideal")
            out.noise = NoiseChoice::Ideal;
        else if (a[0] == "calibrated")
            out.noise = NoiseChoice::Calibrated;
        else if (a[0].starts_with("custom:")) {
            out.noise = NoiseChoice::Custom;
            out.noiseFile = a[0].substr(7);
        } else
            return bad("unknown noise source '" + a[0] + "'");
        return;
    }
    if (n == "layout") {
        if (a.size() != 1)
            return bad("expected a layout policy");
        static const std::pair<const char*, LayoutChoice> m[] = {
            {"physical", LayoutChoice::Physical},
            {"trivial", LayoutChoice::Trivial},
            {"dense", LayoutChoice::Dense},
            {"vf2", LayoutChoice::Vf2},
            {"noise_aware", LayoutChoice::NoiseAware}};
        for (auto& [k, v] : m)
            if (a[0] == k) {
                out.layout = v;
                return;
            }
        return bad("unknown layout policy '" + a[0] + "'");
    }
    if (n == "routing") {
        if (a.size() != 1)
            return bad("expected sabre | none");
        if (a[0] == "sabre")
            out.routing = RoutingChoice::Sabre;
        else if (a[0] == "none")
            out.routing = RoutingChoice::None;
        else
            return bad("unknown routing '" + a[0] + "'");
        return;
    }
    if (n == "optimize") {
        std::uint64_t v;
        if (a.size() != 1 || !toU64(a[0], v) || v > 2)
            return bad("optimize level must be 0, 1 or 2");
        out.optimize = static_cast<int>(v);
        return;
    }
    if (n == "sweep") {
        Sweep s;
        if (a.size() >= 2 && a[1] == "in") {
            s.isRange = false;
            s.input = a[0];
            std::string rest;
            for (std::size_t i = 2; i < a.size(); ++i)
                rest += a[i];
            for (auto& it : splitSet(rest)) {
                double v;
                if (!toDouble(it, v))
                    return bad("bad set value '" + it + "'");
                s.values.push_back(v);
            }
            if (s.values.empty())
                return bad("empty sweep set");
            out.sweeps.push_back(s);
            return;
        }
        if (a.size() == 7 && a[1] == "from" && a[3] == "to" && a[5] == "step") {
            s.input = a[0];
            if (!toDouble(a[2], s.from) || !toDouble(a[4], s.to) || !toDouble(a[6], s.step))
                return bad("sweep bounds must be numbers");
            if (s.step == 0 || s.count() == 0)
                return bad("sweep step has wrong sign or is zero");
            out.sweeps.push_back(s);
            return;
        }
        return bad("expected '<input> from a to b step s' or '<input> in {v1, v2}'");
    }
    if (n == "probe") {
        if (a.empty())
            return bad("expected probe kind");
        Probe pr;
        pr.kind = a[0];
        if (pr.kind != "state" && pr.kind != "bloch" && pr.kind != "entanglement" &&
            pr.kind != "density")
            return bad("unknown probe '" + pr.kind + "'");
        for (std::size_t i = 1; i < a.size(); ++i) {
            if (a[i] == "at" && i + 1 < a.size() && a[i + 1] == "barriers") {
                pr.atBarriers = true;
                break;
            }
            pr.qubits.push_back(a[i]);
        }
        if (pr.kind != "state" && pr.qubits.empty())
            return bad("probe '" + pr.kind + "' needs qubit operands");
        out.probes.push_back(pr);
        return;
    }
    if (n == "rb") {
        if (a.size() < 3)
            return bad("expected <n_qubits> <lengths...> <samples>");
        RbSpec r;
        std::uint64_t v;
        if (!toU64(a[0], v) || v < 1 || v > 2)
            return bad("n_qubits must be 1 or 2");
        r.nQubits = static_cast<int>(v);
        for (std::size_t i = 1; i + 1 < a.size(); ++i) {
            if (!toU64(a[i], v) || v == 0)
                return bad("length must be a positive integer");
            r.lengths.push_back(static_cast<int>(v));
        }
        if (!toU64(a.back(), v) || v == 0)
            return bad("samples must be a positive integer");
        r.samples = static_cast<int>(v);
        out.rb = r;
        return;
    }
    if (n == "pulse_level") {
        if (a.size() != 1 || (a[0] != "on" && a[0] != "off"))
            return bad("expected on | off");
        out.pulseLevel = a[0] == "on";
        return;
    }
    if (n == "snapshot_cadence") {
        if (a.size() != 1)
            return bad("expected gate | layer | barrier | <duration>");
        out.snapshotCadence = a[0];
        return;
    }
    if (n == "alignment") {
        if (a.size() != 1 || (a[0] != "left" && a[0] != "sequential" && a[0] != "right"))
            return bad("expected left | sequential | right");
        out.alignment.push_back(a[0]);
        return;
    }
    if (n == "assert") {
        std::string e;
        for (auto& x : a) {
            if (!e.empty())
                e += ' ';
            e += x;
        }
        if (e.empty())
            return bad("empty assertion");
        out.asserts.push_back(e);
        return;
    }
    if (n == "mapping" || n == "schedule" || n == "estimate")
        return; // emitted by the compiler exporter (23 §4); accepted on re-import
    diags.push_back(Diagnostics::make("QL2050", span, n));
}

} // namespace qlab::lang
