#pragma once
// Spec 17 §6 — CPU mesh store of the scene. Generated meshes are cached by a canonical parameter
// key so repeated parts (attenuators, clamps, airbridges) share one MeshData and one GPU upload.
// Meshes that are re-evaluated at runtime (wiring splines in the exploded view, spec 17 §7.4) are
// replaced in place and carry a version the renderer compares before re-uploading.
#include "Graphics/Mesh.hpp"
#include "Lab/Types.hpp"
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace qlab::lab {

struct MeshHandle {
    static constexpr std::uint32_t kInvalid = 0xFFFFFFFFu;
    std::uint32_t index = kInvalid;
    bool valid() const { return index != kInvalid; }
    auto operator<=>(const MeshHandle&) const = default;
};

class MeshLibrary {
public:
    // Stores `mesh`. A non-empty `key` already present returns the cached handle and drops `mesh`.
    MeshHandle add(gfx::MeshData mesh, std::string key = {});
    // Cached handle for `key`, or an invalid handle.
    MeshHandle find(std::string_view key) const;
    // Replaces the data of an existing mesh and increments its version.
    void replace(MeshHandle h, gfx::MeshData mesh);

    const gfx::MeshData& data(MeshHandle h) const { return entries_.at(h.index).data; }
    // Object-space bounds; `valid()` is false for an empty mesh.
    const gfx::Aabb& bounds(MeshHandle h) const { return entries_.at(h.index).bounds; }
    std::uint32_t version(MeshHandle h) const { return entries_.at(h.index).version; }
    std::size_t triangles(MeshHandle h) const { return h.valid() ? entries_.at(h.index).data.triangleCount() : 0; }

    std::size_t size() const { return entries_.size(); }
    std::size_t cacheHits() const { return hits_; }
    std::size_t uniqueTriangles() const;
    std::size_t bytes() const;

private:
    struct Entry {
        gfx::MeshData data;
        gfx::Aabb bounds;
        std::uint32_t version = 0;
    };
    std::vector<Entry> entries_;
    std::unordered_map<std::string, std::uint32_t> byKey_;
    std::size_t hits_ = 0;
};

// Object-space bounds of mesh vertices (emptyAabb() for an empty mesh).
gfx::Aabb meshBounds(const gfx::MeshData& m);

} // namespace qlab::lab
