#pragma once
// Spec 14 §11, spec 13 §6 — text helpers shared by `dump`, `toQasm` and the Build pass. Internal.
#include "IR/Types.hpp"
#include "Lang/Ast.hpp"
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace qlab::ir::detail {

// Shortest decimal that parses back to the same double (std::to_chars); −0 prints as "0".
std::string formatReal(double v);
// The same number spelled as a floating literal ("5.0", "1e-05") so the lexer keeps it a float.
std::string formatFloatLiteral(double v);
// Duration as OpenQASM text: picoseconds as ns with up to three decimals, plus dt ("1.5ns + 4dt").
std::string qasmDuration(const Duration& d);
// Replacement literal for a global name inside a calibration block (bound inputs and constants).
using NameResolver = std::function<std::optional<std::string>(std::string_view)>;
// A `cal { … }` or `defcal … { … }` statement re-emitted as OpenQASM 3 / OpenPulse text; empty for
// any other statement kind.
std::string calibrationText(const lang::Stmt& s, const NameResolver& resolve);

} // namespace qlab::ir::detail
