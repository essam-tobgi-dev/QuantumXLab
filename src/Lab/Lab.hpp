#pragma once
// Umbrella header for qlab::lab (spec 17) — the inspectable 3D laboratory.
//
//   buildScene("sc_lab_standard")  → Scene: the depth-first node array, its meshes and bindings
//   Interaction                    → hover, selection, focus, explode, cutaway, X-ray, layers,
//                                    search-to-select and camera bookmarks (all headless)
//   SceneRenderer                  → culling, LOD, instancing, picking ids, selection label
//   LabOverlays + BindingRegistry  → live values with their fidelity class
#include "Lab/Binding.hpp"
#include "Lab/BindingRegistry.hpp"
#include "Lab/Build.hpp"
#include "Lab/Catalog.hpp"
#include "Lab/ChipLayout.hpp"
#include "Lab/Generators.hpp"
#include "Lab/Interaction.hpp"
#include "Lab/Layout.hpp"
#include "Lab/Materials.hpp"
#include "Lab/MeshLibrary.hpp"
#include "Lab/MeshOps.hpp"
#include "Lab/Overlays.hpp"
#include "Lab/Scene.hpp"
#include "Lab/SceneRenderer.hpp"
#include "Lab/Types.hpp"
