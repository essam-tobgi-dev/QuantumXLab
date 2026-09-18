// Spec 17 §11/§2 — scene construction entry point and node helpers (room and props: BuildRoom.cpp).
#include "Core/Paths.hpp"
#include "Core/Timer.hpp"
#include "Hardware/Hardware.hpp"
#include "Lab/Builder.hpp"
#include "Lab/MeshOps.hpp"
#include <algorithm>
#include <format>

namespace qlab::lab {

ComponentId SceneBuilder::addGroup(ComponentId parent, std::string instance, std::string display, Group group,
                                   const Transform& local) {
    Node n;
    n.kind = NodeKind::Group;
    n.instanceName = std::move(instance);
    n.displayName = std::move(display);
    n.group = group;
    n.local = local;
    n.pickable = false;
    return scene_.add(std::move(n), parent);
}

Result<ComponentId> SceneBuilder::addComponent(ComponentId parent, NodeSpec spec) {
    const ComponentDescriptor* d = scene_.catalog().find(spec.descriptor);
    if (!d)
        return fail(kErrScene,
                    std::format("node '{}' names component '{}', which has no descriptor", spec.instance, spec.descriptor));
    Node n;
    n.kind = NodeKind::Component;
    n.descriptorId = d->id;
    n.instanceName = spec.instance.empty() ? d->id : std::move(spec.instance);
    n.displayName = spec.display.empty() ? d->name : std::move(spec.display);
    n.local = spec.local;
    n.group = spec.group;
    n.material = std::move(spec.material);
    n.tint = spec.tint;
    n.params = std::move(spec.params);
    n.assembly = spec.assembly;
    n.assemblyIndex = spec.assemblyIndex;
    n.pickable = spec.pickable;
    const std::string generator = spec.generator.empty() ? d->generator : spec.generator;
    n.can = generator == "Can";
    n.xrayFade = spec.group == Group::FridgeExterior || d->category == "shield" || d->category == "vacuum";
    GenParams params = GenParams::merged(d->geometry, spec.overrides);
    for (const auto& rule : d->lod) {
        LodLevel level;
        level.maxDistance_m = rule.maxDistance_m;
        level.detail = rule.detail;
        if (rule.detail != Detail::Hidden && spec.authoredMesh != nullptr) {
            if (spec.authoredMesh->triangleCount() > 0)
                level.mesh = scene_.meshes().add(gfx::MeshData(*spec.authoredMesh));
        } else if (rule.detail != Detail::Hidden) {
            auto mesh = generateMesh(generator, params, GenContext{rule.detail, spec.unitScale});
            if (!mesh) {
                mesh.error().notes.push_back("node: " + n.instanceName);
                return std::unexpected(std::move(mesh.error()));
            }
            if (mesh->triangleCount() > 0)
                level.mesh = scene_.meshes().add(std::move(*mesh),
                                                 spec.cacheMesh ? params.cacheKey(generator, rule.detail, spec.unitScale)
                                                                : std::string{});
        }
        n.lods.push_back(level);
    }
    n.localBounds = n.finestMesh().valid() ? scene_.meshes().bounds(n.finestMesh()) : emptyAabb();
    for (const auto& row : d->specSheet) { // spec 17 §5: bind the live rows to this instance
        if (!row.binding) continue;
        auto path = substitutePath(*row.binding, n.params);
        if (!path) continue;
        auto split = splitBindingRoot(*path);
        if (!split) continue;
        Binding b;
        b.root = split->first;
        b.path = split->second;
        b.field = row.field;
        b.unit = row.unit;
        b.cls = row.cls;
        b.simulatorOnly = row.simulatorOnly;
        n.bindings.push_back(std::move(b));
    }
    if (spec.spline) n.spline = std::move(spec.spline);
    return scene_.add(std::move(n), parent);
}

ComponentId SceneBuilder::addScenery(ComponentId parent, std::string instance, std::string display, Group group,
                                     const Transform& local, gfx::MeshData mesh, std::string material) {
    Node n;
    n.kind = NodeKind::Scenery;
    n.instanceName = std::move(instance);
    n.displayName = std::move(display);
    n.group = group;
    n.local = local;
    n.material = std::move(material);
    n.pickable = false; // scenery has no Inspectable, so it must never answer a pick (spec 17 §12)
    LodLevel level;
    level.maxDistance_m = 1e9;
    level.detail = Detail::Full;
    if (mesh.triangleCount() > 0) level.mesh = scene_.meshes().add(std::move(mesh));
    n.lods.push_back(level);
    n.localBounds = n.finestMesh().valid() ? scene_.meshes().bounds(n.finestMesh()) : emptyAabb();
    return scene_.add(std::move(n), parent);
}

ComponentId SceneBuilder::addProp(ComponentId parent, NodeSpec spec, const gfx::MeshData& fallback) {
    if (scene_.catalog().contains(spec.descriptor)) {
        // Keep the authored composite; the descriptor is what makes the prop inspectable.
        if (fallback.triangleCount() > 0) spec.authoredMesh = &fallback;
        spec.cacheMesh = false;
        auto id = addComponent(parent, spec);
        if (id) return *id;
        note(id.error().message);
        return ComponentId{0};
    }
    if (std::find(missing_.begin(), missing_.end(), spec.descriptor) == missing_.end()) {
        missing_.push_back(spec.descriptor);
        note(std::format("prop '{}' has no component.json: drawn as scenery, not selectable (spec 17 §1)", spec.descriptor));
    }
    return addScenery(parent, spec.instance.empty() ? spec.descriptor : spec.instance,
                      spec.display.empty() ? spec.descriptor : spec.display, spec.group, spec.local, fallback,
                      spec.material);
}

double SceneBuilder::stageHeight(cryo::Stage s) const {
    for (const auto& st : layout_.stages)
        if (st.stage == s) return st.height_m;
    return 0.0;
}
double SceneBuilder::stageRadius(cryo::Stage s) const {
    for (const auto& st : layout_.stages)
        if (st.stage == s) return st.radius_m;
    return 0.2;
}
bool SceneBuilder::hasStage(cryo::Stage s) const {
    for (const auto& st : layout_.stages)
        if (st.stage == s) return true;
    return false;
}

namespace {
// Device wiring files and cryo::parseWiring agree on the envelope kind "wiring".
Result<cryo::Wiring> loadDeviceWiring(const std::filesystem::path& path) {
    QXL_TRY_ASSIGN(std::string text, core::readTextFile(path));
    auto w = cryo::parseWiring(text);
    if (!w) w.error().notes.push_back("file: " + path.string());
    return w;
}
} // namespace

Result<Scene> buildScene(const std::filesystem::path& layoutDirOrId, const BuildOptions& options) {
    core::Timer timer;
    QXL_TRY_ASSIGN(LayoutSpec layout, loadLayout(layoutDirOrId));
    RoutingSpec routing;
    std::error_code ec;
    if (std::filesystem::exists(layout.routingFile, ec)) {
        QXL_TRY_ASSIGN(routing, loadRouting(layout.routingFile));
    }
    QXL_TRY_ASSIGN(ComponentCatalog catalog, ComponentCatalog::load(options.componentDir));
    Scene scene(std::move(catalog));
    scene.layout() = std::move(layout);
    SceneBuilder builder(scene, scene.layout(), routing, options.flipChipBumps);
    ComponentId root = builder.addGroup(ComponentId{0}, "laboratory", "Laboratory", Group::Room);

    std::string deviceId = options.deviceOverride.empty() ? scene.layout().device : options.deviceOverride;
    std::optional<hw::LoadedDevice> device;
    if (!deviceId.empty()) {
        auto loaded = hw::loadShippedDevice(deviceId);
        if (!loaded) return std::unexpected(loaded.error());
        for (const auto& w : loaded->warnings) scene.diagnostics().push_back("device: " + w);
        device = std::move(*loaded);
        if (deviceId != scene.layout().device) { // an overridden device brings its own wiring
            std::filesystem::path wiring = device->device.directory / "wiring.json";
            if (std::filesystem::exists(wiring, ec)) scene.layout().wiringFile = wiring;
            scene.layout().device = deviceId;
        }
    }

    if (!options.wiringOverride.empty()) scene.layout().wiringFile = options.wiringOverride;
    QXL_TRY(builder.buildRoom(root));
    if (scene.layout().hasFridge()) {
        QXL_TRY(builder.buildFridge(root));
        scene.updateTransforms(); // the wiring anchors its splines to the flattened stage positions
        if (!scene.layout().wiringFile.empty()) {
            QXL_TRY_ASSIGN(cryo::Wiring wiring, loadDeviceWiring(scene.layout().wiringFile));
            if (options.validateWiring)
                if (auto st = cryo::validateWiring(wiring); !st)
                    scene.diagnostics().push_back("wiring rules (spec 11 §5): " + st.error().message);
            QXL_TRY(builder.buildWiring(root, wiring));
        }
    }
    QXL_TRY(builder.buildRacks(root));
    QXL_TRY(builder.buildGasHandling(root));
    QXL_TRY(builder.buildProps(root));
    if (device && options.buildChip && hw::isTransmon(device->device.technology)) {
        QXL_TRY_ASSIGN(ChipLayout chip, layoutChip(device->device, &device->calibration, options.chip));
        scene.chipLayout() = std::move(chip);
        ComponentId parent = builder.puckNode().value != 0 ? builder.puckNode() : root;
        QXL_TRY(builder.buildChip(parent, *device, *scene.chipLayout()));
    } else if (device && scene.layout().chamberPosition_m) {
        QXL_TRY(builder.buildIonLab(root, *device));
    }
    QXL_TRY(scene.finalize(timer.ms()));
    return scene;
}

} // namespace qlab::lab
