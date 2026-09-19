#pragma once
// Spec 13 §7 — typed `pragma qlab.*` directives.
#include "Lang/Ast.hpp"
#include "Lang/Diagnostics.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace qlab::lang {

enum class BackendChoice { Auto, StateVector, DensityMatrix, Stabilizer, Lindblad };
enum class NoiseChoice { Ideal, Calibrated, Custom };
enum class LayoutChoice { Physical, Trivial, Dense, Vf2, NoiseAware };
enum class RoutingChoice { Sabre, None };

struct Sweep {
    std::string input;
    bool isRange = true;
    double from = 0, to = 0, step = 1; // range form
    std::vector<double> values;        // set form
    std::size_t count() const;
    std::vector<double> grid() const;
};
struct Probe {
    std::string kind;                // state | bloch | entanglement | density
    std::vector<std::string> qubits; // operand texts (q[0], $3)
    bool atBarriers = false;
};
struct RbSpec {
    int nQubits = 1;
    std::vector<int> lengths;
    int samples = 1;
};

struct Pragmas {
    std::optional<std::string> device;
    BackendChoice backend = BackendChoice::Auto;
    std::optional<std::uint64_t> shots;
    std::optional<std::uint64_t> seed;
    NoiseChoice noise = NoiseChoice::Calibrated;
    std::string noiseFile;
    std::optional<LayoutChoice> layout;
    RoutingChoice routing = RoutingChoice::Sabre;
    int optimize = 1;
    std::vector<Sweep> sweeps;
    std::vector<Probe> probes;
    std::optional<RbSpec> rb;
    std::optional<bool> pulseLevel;
    std::string snapshotCadence = "gate";
    std::vector<std::string> alignment; // spec 10 §alignment values
    std::vector<std::string> asserts;   // raw `qlab.assert` expressions (spec 22 §6)
    bool physicalLayout() const { return layout && *layout == LayoutChoice::Physical; }
};

// Interprets a PragmaStmt into `out`; reports QL2014 (malformed), QL2050 (unknown), QL2051
// (ignored).
void applyPragma(const PragmaStmt& p, const SourceSpan& span, Pragmas& out,
                 std::vector<Diagnostic>& diags);

} // namespace qlab::lang
