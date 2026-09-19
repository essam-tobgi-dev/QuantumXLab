#include "Lab/MeshLibrary.hpp"

namespace qlab::lab {

gfx::Aabb meshBounds(const gfx::MeshData& m) {
    gfx::Aabb b = emptyAabb();
    for (const auto& v : m.vertices)
        b.expand(glm::dvec3(v.position));
    return b;
}

MeshHandle MeshLibrary::add(gfx::MeshData mesh, std::string key) {
    if (!key.empty()) {
        if (auto it = byKey_.find(key); it != byKey_.end()) {
            ++hits_;
            return MeshHandle{it->second};
        }
    }
    auto index = static_cast<std::uint32_t>(entries_.size());
    gfx::Aabb b = meshBounds(mesh);
    entries_.push_back(Entry{std::move(mesh), b, 0});
    if (!key.empty())
        byKey_.emplace(std::move(key), index);
    return MeshHandle{index};
}

MeshHandle MeshLibrary::find(std::string_view key) const {
    auto it = byKey_.find(std::string(key));
    return it == byKey_.end() ? MeshHandle{} : MeshHandle{it->second};
}

void MeshLibrary::replace(MeshHandle h, gfx::MeshData mesh) {
    Entry& e = entries_.at(h.index);
    e.bounds = meshBounds(mesh);
    e.data = std::move(mesh);
    ++e.version;
}

std::size_t MeshLibrary::uniqueTriangles() const {
    std::size_t n = 0;
    for (const auto& e : entries_)
        n += e.data.triangleCount();
    return n;
}

std::size_t MeshLibrary::bytes() const {
    std::size_t n = 0;
    for (const auto& e : entries_)
        n += e.data.vertices.size() * sizeof(gfx::Vertex) +
             e.data.indices.size() * sizeof(std::uint32_t);
    return n;
}

} // namespace qlab::lab
