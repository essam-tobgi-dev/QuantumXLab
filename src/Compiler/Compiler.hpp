#pragma once
// Umbrella header for qlab::compiler (spec 14): pass manager and the fixed pipeline, decomposition,
// optimization, layout, SABRE routing, scheduling, pulse lowering, equivalence checking,
// diagnostics, incremental compilation and compiled-program export.
//
// Entry points: `compile` / `compileSource` (Compile.hpp), `PassManager::standard` (Pass.hpp),
// `checkEquivalence` (Equivalence.hpp), `exportQasm` (Compile.hpp), `IncrementalCompiler`.
#include "Compiler/CircuitUtil.hpp"
#include "Compiler/Commutation.hpp"
#include "Compiler/Compile.hpp"
#include "Compiler/Coupling.hpp"
#include "Compiler/Decompose.hpp"
#include "Compiler/Equivalence.hpp"
#include "Compiler/Euler.hpp"
#include "Compiler/Incremental.hpp"
#include "Compiler/Kak.hpp"
#include "Compiler/LayoutPass.hpp"
#include "Compiler/Optimize.hpp"
#include "Compiler/Pass.hpp"
#include "Compiler/PulseLower.hpp"
#include "Compiler/Route.hpp"
#include "Compiler/Rules.hpp"
#include "Compiler/Schedule.hpp"
#include "Compiler/Tableau.hpp"
#include "Compiler/Target.hpp"
#include "Compiler/Types.hpp"
