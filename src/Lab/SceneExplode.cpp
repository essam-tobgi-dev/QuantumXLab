// Spec 17 §7.4 — exploded view. Assembly 1 separates the fridge stages from the fixed top plate
// (0.25 m per stage at full extension) and re-evaluates every wiring spline through the moved
// clamps; assembly 2 lifts the package PCB and the chip; assembly 3 slides rack units out.
#include "Lab/Generators.hpp"
#include "Lab/Scene.hpp"
#include <algorithm>
#include <map>

namespace qlab::lab {

namespace {
constexpr double kStageSeparation_m = 0.25; // spec 17 §7.4
constexpr double kRackSlide_m = 0.35;
constexpr double kPcbLift_m = 0.018, kChipLift_m = 0.008;
} // namespace

void Scene::setExplode(Assembly a, double s) {
    s = std::clamp(s, 0.0, 1.0);
    explode_[static_cast<std::size_t>(a)] = s;
    switch (a) {
    case Assembly::FridgeStages:
        for (std::size_t k = 0; k < stageNodes_.size(); ++k)
            if (Node* n = node(stageNodes_[k]))
                n->explodeOffset = {0.0, -kStageSeparation_m * static_cast<double>(k) * s, 0.0};
        regenerateSplines();
        break;
    case Assembly::ChipPackage:
        for (auto& n : nodes_)
            if (n.assembly == Assembly::ChipPackage)
                n.explodeOffset = {0.0, (n.assemblyIndex == 1 ? kPcbLift_m : kChipLift_m) * s, 0.0};
        break;
    case Assembly::RackUnits:
        for (auto& n : nodes_)
            if (n.assembly == Assembly::RackUnits)
                n.explodeOffset = {0.0, 0.0, kRackSlide_m * s};
        break;
    case Assembly::None:
        break;
    }
    updateTransforms();
}

void Scene::regenerateSplines() {
    const double s = explode_[static_cast<std::size_t>(Assembly::FridgeStages)];
    std::array<double, cryo::kStageCount> dy{};
    for (std::size_t k = 0; k < dy.size(); ++k)
        dy[k] = -kStageSeparation_m * static_cast<double>(k) * s;
    std::map<std::uint32_t, int> stageOf;
    for (std::size_t k = 0; k < stageNodes_.size(); ++k)
        if (stageNodes_[k].value != 0)
            stageOf[stageNodes_[k].value] = static_cast<int>(k);

    for (auto& n : nodes_) {
        if (!n.spline)
            continue;
        int frame = -1;
        for (const Node* p = node(n.parent); p; p = node(p->parent))
            if (auto it = stageOf.find(p->id.value); it != stageOf.end()) {
                frame = it->second;
                break;
            }
        double base = frame >= 0 ? dy[static_cast<std::size_t>(frame)] : 0.0;
        core::Json pts = core::Json::array();
        for (const auto& anchor : n.spline->points) {
            double off =
                (anchor.stage >= 0 ? dy[static_cast<std::size_t>(anchor.stage)] : 0.0) - base;
            pts.push_back({anchor.restLocal.x, anchor.restLocal.y + off, anchor.restLocal.z});
        }
        GenParams params(n.spline->geometry);
        params.set("points_m", std::move(pts));
        for (auto& lod : n.lods) {
            if (!lod.mesh.valid())
                continue;
            if (auto m = generateMesh(n.spline->generator, params, GenContext{lod.detail, 1.0}))
                meshes_.replace(lod.mesh, std::move(*m));
        }
        n.localBounds = meshes_.bounds(n.finestMesh());
    }
}

} // namespace qlab::lab
