#pragma once
// The fidelity class is project-wide vocabulary and is defined in Core (spec 00 §5, spec 02 §4);
// this header keeps the `qlab::data::` spelling that Layer-1 data records and every higher layer
// already use.
#include "Core/Fidelity.hpp"

namespace qlab::data {
using FidelityClass = qlab::FidelityClass;
using qlab::fidelityName;
using qlab::weakest;
} // namespace qlab::data
