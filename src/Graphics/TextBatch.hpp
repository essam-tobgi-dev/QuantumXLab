#pragma once
// Spec 18 §3 — SDF text: 3D billboards (world anchor, screen-sized) and screen-space labels.
#include "Graphics/GlObjects.hpp"
#include "Graphics/SdfFont.hpp"
#include <glm/glm.hpp>
#include <string_view>
#include <vector>

namespace qlab::gfx {

enum class TextAnchor { TopLeft, Center, BottomLeft, TopCenter, BottomCenter };

struct TextVertex {
    glm::vec3 anchor;   // origin-relative position (billboard) or (px, py, 0) for screen mode
    glm::vec2 offset;   // pixel offset of the quad corner from the anchor
    glm::vec2 uv;
    glm::vec4 color;
    float screenSpace;  // 1 = anchor is in pixels
};

class TextBatch {
public:
    explicit TextBatch(const SdfFont* font = nullptr) : font_(font) {}
    void setFont(const SdfFont* f) { font_ = f; }
    void clear() { verts_.clear(); }
    // 3D anchors are WORLD positions; the renderer sets the origin to the camera position each
    // frame and it is subtracted in double precision before the float upload (spec 18 §6).
    void setOrigin(const glm::dvec3& worldOrigin) { origin_ = worldOrigin; }
    // Label anchored at a world position; text is sizePx tall on screen regardless of distance.
    void label3D(const glm::dvec3& world, std::string_view utf8, float sizePx, const glm::vec4& color,
                 TextAnchor anchor = TextAnchor::BottomCenter, glm::vec2 pixelOffset = {0, 0});
    // (glm::vec3 anchors convert implicitly.)
    void label2D(glm::vec2 px, std::string_view utf8, float sizePx, const glm::vec4& color,
                 TextAnchor anchor = TextAnchor::TopLeft);
    bool empty() const { return verts_.empty(); }
    void upload();
    void draw() const; // expects the `text_sdf` program bound and the atlas on unit 0
    const SdfFont* font() const { return font_; }
private:
    void emit(const glm::vec3& anchor, std::string_view s, float sizePx, const glm::vec4& c, TextAnchor a, glm::vec2 off, bool screen);
    const SdfFont* font_;
    glm::dvec3 origin_{0.0};
    std::vector<TextVertex> verts_;
    Buffer vbo_;
    VertexArray vao_;
    bool init_ = false;
    std::size_t uploaded_ = 0;
};

} // namespace qlab::gfx
