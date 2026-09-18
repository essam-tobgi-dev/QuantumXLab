#include "Graphics/TextBatch.hpp"
namespace qlab::gfx {
void TextBatch::label3D(const glm::dvec3& w, std::string_view s, float px, const glm::vec4& c, TextAnchor a, glm::vec2 off) {
    emit(glm::vec3(w - origin_), s, px, c, a, off, false); // world → origin-relative in double
}
void TextBatch::label2D(glm::vec2 p, std::string_view s, float px, const glm::vec4& c, TextAnchor a) {
    emit(glm::vec3(p, 0), s, px, c, a, {0, 0}, true);
}
void TextBatch::emit(const glm::vec3& anchor, std::string_view s, float px, const glm::vec4& c, TextAnchor a, glm::vec2 off, bool screen) {
    if (!font_) return;
    const auto& atlas = font_->atlas();
    float scale = px / static_cast<float>(atlas.nominalPx);
    glm::vec2 size = font_->measure(s, px);
    glm::vec2 origin = off; // pen origin (baseline-left) relative to anchor, y down in screen px
    float asc = atlas.ascent * scale, desc = atlas.descent * scale;
    switch (a) {
    case TextAnchor::TopLeft: origin += glm::vec2(0, asc); break;
    case TextAnchor::Center: origin += glm::vec2(-size.x * 0.5f, (asc - desc) * 0.5f); break;
    case TextAnchor::BottomLeft: origin += glm::vec2(0, -desc); break;
    case TextAnchor::TopCenter: origin += glm::vec2(-size.x * 0.5f, asc); break;
    case TextAnchor::BottomCenter: origin += glm::vec2(-size.x * 0.5f, -desc); break;
    }
    float pen = 0;
    float ss = screen ? 1.f : 0.f;
    for (unsigned cp : decodeUtf8(s)) {
        const Glyph* g = font_->glyph(cp);
        if (!g) continue;
        // quad in pixel space, y down: top-left at (pen + bearing.x, -bearing.y)
        glm::vec2 tl = origin + glm::vec2(pen + g->bearing.x * scale, -g->bearing.y * scale);
        glm::vec2 br = tl + g->size * scale;
        TextVertex v0{anchor, tl, g->uv0, c, ss};
        TextVertex v1{anchor, {br.x, tl.y}, {g->uv1.x, g->uv0.y}, c, ss};
        TextVertex v2{anchor, br, g->uv1, c, ss};
        TextVertex v3{anchor, {tl.x, br.y}, {g->uv0.x, g->uv1.y}, c, ss};
        verts_.insert(verts_.end(), {v0, v1, v2, v0, v2, v3});
        pen += g->advance * scale;
    }
}
void TextBatch::upload() {
    if (!init_) { vbo_ = Buffer(GL_ARRAY_BUFFER); vao_ = VertexArray(); init_ = true; }
    vbo_.setData(verts_, BufferUsage::Stream);
    const GLsizei stride = sizeof(TextVertex);
    VertexAttrib a[] = {
        {0, 3, GL_FLOAT, false, stride, offsetof(TextVertex, anchor)},
        {1, 2, GL_FLOAT, false, stride, offsetof(TextVertex, offset)},
        {2, 2, GL_FLOAT, false, stride, offsetof(TextVertex, uv)},
        {3, 4, GL_FLOAT, false, stride, offsetof(TextVertex, color)},
        {4, 1, GL_FLOAT, false, stride, offsetof(TextVertex, screenSpace)},
    };
    vao_.setAttribs(vbo_, a);
    VertexArray::unbind();
    uploaded_ = verts_.size();
}
void TextBatch::draw() const {
    if (!init_ || !uploaded_) return;
    vao_.bind();
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(uploaded_));
}
} // namespace qlab::gfx
