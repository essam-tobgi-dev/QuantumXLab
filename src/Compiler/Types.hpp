#pragma once
// Spec 14 §1 — compiler vocabulary: policies, per-pass metrics, layouts, options, error codes.
#include "Core/Error.hpp"
#include "Core/StrongType.hpp"
#include "IR/Build.hpp"
#include "IR/Circuit.hpp"
#include "Lang/Diagnostics.hpp"
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace qlab::hw { struct Device; struct Calibration; }
namespace qlab::pulse { class PulseLibrary; }

namespace qlab::compiler {

enum class OptimizeLevel : std::uint8_t { O0 = 0, O1 = 1, O2 = 2 };
// `pragma qlab.layout physical` is a property of the circuit (`Circuit::isPhysical()`), not a policy.
enum class LayoutPolicy : std::uint8_t { Trivial, Dense, Vf2, NoiseAware };
enum class RoutingPolicy : std::uint8_t { Sabre, None };
enum class SchedulePolicy : std::uint8_t { Asap, Alap };

std::string_view layoutPolicyName(LayoutPolicy p);
std::string_view routingPolicyName(RoutingPolicy p);
std::string_view schedulePolicyName(SchedulePolicy p);

// Spec 14 §1: every pass reports these; the UI shows the delta per pass. Counts include the
// bodies of Branch/Loop/Box nodes; `depth` is `ir::Circuit::depth()` of the top level.
struct PassMetrics {
    std::uint32_t gateCount = 0;       // Gate nodes (measure/reset/directives excluded)
    std::uint32_t twoQubitCount = 0;   // gates on exactly two wires
    std::uint32_t tCount = 0;          // t + tdg
    std::uint32_t swapCount = 0;       // swaps inserted by routing (kept after they are decomposed)
    std::uint32_t depth = 0;
    Picoseconds estimatedDuration{0};  // critical path; 0 until scheduled
    bool operator==(const PassMetrics&) const = default;
};

struct PassResult {
    std::string pass;                              // IPass::name()
    std::vector<lang::Diagnostic> diagnostics;     // QL4xxx raised by this pass
    PassMetrics before, after;
    std::chrono::microseconds wallTime{0};
};

// Injective map program qubit → physical qubit (spec 14 §7). `v2p[v]` is the device qubit that
// holds program qubit v; the initial layout holds at t = 0, the final one after routing.
struct Layout {
    std::vector<std::uint32_t> v2p;
    bool empty() const { return v2p.empty(); }
    std::size_t size() const { return v2p.size(); }
    std::uint32_t physical(std::uint32_t v) const { return v2p[v]; }
    std::optional<std::uint32_t> virtualOf(std::uint32_t p) const;
    bool injective() const;
    static Layout identity(std::uint32_t n);
    std::string text() const;                      // "q0->$3, q1->$5"
    bool operator==(const Layout&) const = default;
};

// User-facing options. An unset optional takes the program's `pragma qlab.*` value when the
// compile starts from a `lang::Program`, else the documented default.
struct CompileOptions {
    std::optional<OptimizeLevel> level;            // default O1 (`qlab.optimize`)
    std::optional<LayoutPolicy> layout;            // default NoiseAware (`qlab.layout`)
    std::optional<RoutingPolicy> routing;          // default Sabre (`qlab.routing`)
    std::optional<bool> pulseLevel;                // default off (`qlab.pulse_level`)
    std::optional<std::uint64_t> seed;             // routing/layout tie-breaks (`qlab.seed`)
    SchedulePolicy schedule = SchedulePolicy::Asap;
    const pulse::PulseLibrary* pulses = nullptr;   // loaded from the device directory when needed
    ir::ParamMap inputs;                           // values of `input` declarations (spec 14 §2)
    std::uint32_t loopUnrollBound = 65536;
    std::string twoQubitBasis;                     // "" = the device's first native entangler
    bool kak = false;                              // §5.5 two-qubit resynthesis (also on at O2)
    bool verifyEquivalence = true;                 // pass 11, within `equivalenceWorkLimit`
    std::uint64_t equivalenceWorkLimit = 20'000'000; // amplitude updates spent by the automatic check
    bool enforcePulseQubitCap = true;              // QL4040 (spec 15 §2: 5 transmons, 6 ions)
    std::uint32_t pulseQubitCap = 0;               // 0 = technology default
    std::uint32_t vf2StateBudget = 1'000'000;      // spec 14 §7
};

// Resolved settings of one compile (spec 14 §1 `CompileContext`).
struct CompileContext {
    const hw::Device* device = nullptr;            // nullptr: device-independent compile to {U, cx}
    const hw::Calibration* calibration = nullptr;
    const pulse::PulseLibrary* pulses = nullptr;
    OptimizeLevel level = OptimizeLevel::O1;
    LayoutPolicy layout = LayoutPolicy::NoiseAware;
    RoutingPolicy routing = RoutingPolicy::Sabre;
    SchedulePolicy schedule = SchedulePolicy::Asap;
    bool pulseLevel = false;
    std::uint32_t loopUnrollBound = 65536;
    std::uint64_t seed = 0x51AB5EEDull;
    std::string twoQubitBasis;
    bool kak = false;
    bool verifyEquivalence = true;
    std::uint64_t equivalenceWorkLimit = 20'000'000;
    bool enforcePulseQubitCap = true;
    std::uint32_t pulseQubitCap = 0;
    std::uint32_t vf2StateBudget = 1'000'000;

    static CompileContext from(const CompileOptions& o, const hw::Device* device, const hw::Calibration* calibration);
};

// Error codes of this module. Offsets below 128 mirror the QL4xxx diagnostics
// (`lang::Diagnostics::codeFor("QL4030") == Compiler_ + 30`); 128 upward belongs to the IR.
namespace err {
inline constexpr ErrorCode UnresolvedDt = ErrorCode::Compiler_ + 10;        // QL4010
inline constexpr ErrorCode CouplingViolation = ErrorCode::Compiler_ + 30;   // QL4030
inline constexpr ErrorCode PulseQubitCap = ErrorCode::Compiler_ + 40;       // QL4040
inline constexpr ErrorCode TooManyControls = ErrorCode::Compiler_ + 50;     // QL4050
inline constexpr ErrorCode Vf2Budget = ErrorCode::Compiler_ + 60;           // QL4060 (info)
inline constexpr ErrorCode NotDecomposable = ErrorCode::Compiler_ + 70;     // QL4070
inline constexpr ErrorCode MissingCalibration = ErrorCode::Compiler_ + 80;  // QL4080
inline constexpr ErrorCode ScheduleTooLong = ErrorCode::Compiler_ + 90;     // QL4090
// Conditions without a catalogue id.
inline constexpr ErrorCode NotEquivalent = ErrorCode::Compiler_ + 100;  // pass 11 found a miscompile
inline constexpr ErrorCode BadLayout = ErrorCode::Compiler_ + 101;      // layout not injective / off-device
inline constexpr ErrorCode NoDevice = ErrorCode::Compiler_ + 102;       // pass needs a device or calibration
inline constexpr ErrorCode BadCalibrationBlock = ErrorCode::Compiler_ + 103; // program cal/defcal not lowerable
inline constexpr ErrorCode BoxOverrun = ErrorCode::Compiler_ + 104;     // box body longer than box[d]
inline constexpr ErrorCode Unsupported = ErrorCode::Compiler_ + 105;
inline constexpr ErrorCode TooLarge = ErrorCode::Compiler_ + 106;       // equivalence check above its cap
} // namespace err

} // namespace qlab::compiler
