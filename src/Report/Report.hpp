#pragma once
// Umbrella header for qlab::report (spec 23): the user-facing file formats of QuantumXLab.
//
//   Types.hpp         §1  envelope kinds (`project`, `run_result`, `state_export`), run identity
//   Project.hpp       §2  the `.qxlab` project file, autosave and crash recovery
//   ProgramExport.hpp §5  source and compiled OpenQASM 3, pulse sample CSV
//   ResultExport.hpp  §6  the `run_result` document, packed per-shot memory, sweep CSV
//   TraceExport.hpp   §7  traces and channels as CSV with `#` provenance comments
//   Image.hpp         §8  PNG writing and the baked annotation strip
//   StateExport.hpp   §9  `.npy` state vector / density matrix, stabilizer tableau (Simulator-only)
//   Html.hpp          §10 escaping, data URIs, `{{placeholder}}` substitution, inline SVG figures
//   HtmlReport.hpp    §10 the self-contained HTML run report
//   Import.hpp        §11 OpenQASM 3 / OpenQASM 2 / device directories, and nothing else
#include "Report/Html.hpp"
#include "Report/HtmlReport.hpp"
#include "Report/Image.hpp"
#include "Report/Import.hpp"
#include "Report/ProgramExport.hpp"
#include "Report/Project.hpp"
#include "Report/ResultExport.hpp"
#include "Report/StateExport.hpp"
#include "Report/TraceExport.hpp"
#include "Report/Types.hpp"
