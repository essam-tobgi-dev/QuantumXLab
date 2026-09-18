#pragma once
// Spec 17 §2/§3/§11 — building the laboratory scene from Assets/Lab/Layouts/<id>/ plus the
// device, its wiring and the component catalog.
#include "Lab/ChipLayout.hpp"
#include "Lab/Scene.hpp"
#include <filesystem>
#include <string>

namespace qlab::lab {

struct BuildOptions {
    std::string deviceOverride;          // empty: the layout's `device`
    std::filesystem::path wiringOverride; // empty: the device's wiring.json (a variant preview)
    ChipLayoutOptions chip;
    bool buildChip = true;
    bool validateWiring = true;          // spec 11 §5 chain rules
    // Spec 17 §3.5: flip-chip bumps are drawn when the package is flip-chip; no shipped device
    // declares one, so the App sets this from the package it is showing.
    bool flipChipBumps = false;
    std::filesystem::path componentDir;  // empty: core::assetDir()/Lab/Components
};

// Builds the scene of a layout id ("sc_lab_standard") or directory. Every component node resolves
// to a component.json; the room shell and the props the catalog has no descriptor for are scenery
// (not pickable) and are listed in Scene::diagnostics().
Result<Scene> buildScene(const std::filesystem::path& layoutDirOrId, const BuildOptions& options = {});

} // namespace qlab::lab
