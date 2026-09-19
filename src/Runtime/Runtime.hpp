#pragma once
// Umbrella header for qlab::runtime (spec 15): the session, the execution of a compiled program on
// a simulator backend with calibration-derived noise, the run result, sweeps, and the hardware
// wall-time / fidelity / resource estimators.
//
// Entry points: `Session` (§1), `execute` / `executePulse` (§3), `RunResult` (§4), `SweepGrid`
// (§5), `estimate` with `estimateWallTime` / `estimateFidelityFast` / `estimateQec` (§6–§9).
#include "Runtime/Estimate.hpp"
#include "Runtime/Plan.hpp"
#include "Runtime/Run.hpp"
#include "Runtime/Select.hpp"
#include "Runtime/Session.hpp"
#include "Runtime/Types.hpp"
