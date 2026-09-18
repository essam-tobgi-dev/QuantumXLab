// Spec 15 §9, T12 §8 — the device-vs-device row of an estimate: the same program recompiled for
// every shipped device that can accept it, with its wall time and product fidelity.
#include "Compiler/Compile.hpp"
#include "Runtime/Estimate.hpp"
#include "Runtime/Plan.hpp"

namespace qlab::runtime {

Result<std::vector<DeviceComparison>> compareDevices(const lang::Program& program,
                                                     const compiler::CompileOptions& options, std::uint64_t shots,
                                                     std::string_view skip, std::stop_token stop) {
    std::vector<DeviceComparison> rows;
    for (const std::string& id : hw::shippedDeviceIds()) {
        if (stop.stop_requested()) return fail(ErrorCode::Cancelled, "device comparison cancelled");
        if (id == skip) continue;
        auto loaded = hw::loadShippedDevice(id);
        if (!loaded) continue;
        // A device that cannot compile the circuit (native set, connectivity, qubit count) is simply
        // not in the table; it is not an error of the run (spec 15 §9).
        auto compiled = compiler::compile(program, loaded->device, loaded->calibration, options, stop);
        if (!compiled) continue;
        EstimateInput in;
        in.device = &loaded->device;
        in.calibration = &loaded->calibration;
        in.program = &*compiled;
        in.shots = shots;
        in.usedQubits = usedQubits(compiled->circuit);
        for (ir::NodeId node : compiled->circuit.topologicalOrder())
            if (const auto* m = std::get_if<ir::Measure>(&compiled->circuit.node(node)))
                in.measuredQubits.push_back(m->qubit.index);
        std::sort(in.measuredQubits.begin(), in.measuredQubits.end());
        in.measuredQubits.erase(std::unique(in.measuredQubits.begin(), in.measuredQubits.end()),
                                in.measuredQubits.end());
        auto wall = estimateWallTime(in);
        auto fidelity = estimateFidelityFast(in);
        if (!wall || !fidelity) continue;
        rows.push_back(DeviceComparison{id, wall->valueS, fidelity->fast});
    }
    return rows;
}

} // namespace qlab::runtime
