#include "Graphics/GlObjects.hpp"
#include "Graphics/GlCheck.hpp"
#include "Core/Log.hpp"

namespace qlab::gfx {

// ---------------------------------------------------------------- Buffer
Buffer::Buffer(GLenum target) : target_(target) { glGenBuffers(1, &id_); }
void Buffer::release() { if (id_) { glDeleteBuffers(1, &id_); id_ = 0; } }
void Buffer::bind() const { glBindBuffer(target_, id_); }
void Buffer::bindBase(GLuint index) const { glBindBufferBase(target_, index, id_); }
void Buffer::setData(std::span<const std::byte> bytes, BufferUsage usage) {
    GLenum u = usage == BufferUsage::Static ? GL_STATIC_DRAW : usage == BufferUsage::Dynamic ? GL_DYNAMIC_DRAW : GL_STREAM_DRAW;
    glBindBuffer(target_, id_);
    glBufferData(target_, static_cast<GLsizeiptr>(bytes.size()), bytes.data(), u);
    size_ = bytes.size();
}
void Buffer::update(std::size_t offset, std::span<const std::byte> bytes) {
    glBindBuffer(target_, id_);
    if (offset + bytes.size() > size_) {
        glBufferData(target_, static_cast<GLsizeiptr>(offset + bytes.size()), nullptr, GL_DYNAMIC_DRAW);
        size_ = offset + bytes.size();
    }
    glBufferSubData(target_, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(bytes.size()), bytes.data());
}

// ---------------------------------------------------------------- VertexArray
VertexArray::VertexArray() { glGenVertexArrays(1, &id_); }
void VertexArray::release() { if (id_) { glDeleteVertexArrays(1, &id_); id_ = 0; } }
void VertexArray::bind() const { glBindVertexArray(id_); }
void VertexArray::unbind() { glBindVertexArray(0); }
void VertexArray::setAttribs(const Buffer& buf, std::span<const VertexAttrib> attribs) {
    glBindVertexArray(id_);
    glBindBuffer(GL_ARRAY_BUFFER, buf.id());
    for (const auto& a : attribs) {
        glEnableVertexAttribArray(a.index);
        const void* off = reinterpret_cast<const void*>(a.offset);
        if (a.type == GL_UNSIGNED_INT || a.type == GL_INT)
            glVertexAttribIPointer(a.index, a.components, a.type, a.stride, off);
        else
            glVertexAttribPointer(a.index, a.components, a.type, a.normalized ? GL_TRUE : GL_FALSE, a.stride, off);
        glVertexAttribDivisor(a.index, a.divisor);
    }
    glBindVertexArray(0);
}
void VertexArray::setIndexBuffer(const Buffer& ibo) {
    glBindVertexArray(id_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo.id());
    glBindVertexArray(0);
}

// ---------------------------------------------------------------- Texture2D
void Texture2D::glFormat(TexFormat f, GLenum& internal, GLenum& format, GLenum& type) {
    switch (f) {
    case TexFormat::RGBA8: internal = GL_RGBA8; format = GL_RGBA; type = GL_UNSIGNED_BYTE; break;
    case TexFormat::SRGBA8: internal = GL_SRGB8_ALPHA8; format = GL_RGBA; type = GL_UNSIGNED_BYTE; break;
    case TexFormat::RGBA16F: internal = GL_RGBA16F; format = GL_RGBA; type = GL_HALF_FLOAT; break;
    case TexFormat::RGB16F: internal = GL_RGB16F; format = GL_RGB; type = GL_HALF_FLOAT; break;
    case TexFormat::R11G11B10F: internal = GL_R11F_G11F_B10F; format = GL_RGB; type = GL_UNSIGNED_INT_10F_11F_11F_REV; break;
    case TexFormat::R32UI: internal = GL_R32UI; format = GL_RED_INTEGER; type = GL_UNSIGNED_INT; break;
    case TexFormat::R8: internal = GL_R8; format = GL_RED; type = GL_UNSIGNED_BYTE; break;
    case TexFormat::RG16F: internal = GL_RG16F; format = GL_RG; type = GL_HALF_FLOAT; break;
    case TexFormat::R32F: internal = GL_R32F; format = GL_RED; type = GL_FLOAT; break;
    case TexFormat::Depth24Stencil8: internal = GL_DEPTH24_STENCIL8; format = GL_DEPTH_STENCIL; type = GL_UNSIGNED_INT_24_8; break;
    case TexFormat::Depth32F: internal = GL_DEPTH_COMPONENT32F; format = GL_DEPTH_COMPONENT; type = GL_FLOAT; break;
    }
}
Texture2D::Texture2D(const TexDesc& d, const void* pixels) : desc_(d) {
    glGenTextures(1, &id_);
    allocate(pixels);
}
void Texture2D::allocate(const void* pixels) {
    GLenum internal, format, type;
    glFormat(desc_.format, internal, format, type);
    GLenum tgt = target();
    glBindTexture(tgt, id_);
    if (desc_.samples > 0) {
        glTexImage2DMultisample(tgt, desc_.samples, internal, desc_.width, desc_.height, GL_TRUE);
    } else {
        glTexImage2D(tgt, 0, static_cast<GLint>(internal), desc_.width, desc_.height, 0, format, type, pixels);
        bool integer = desc_.format == TexFormat::R32UI;
        GLint filt = (desc_.linear && !integer) ? GL_LINEAR : GL_NEAREST;
        glTexParameteri(tgt, GL_TEXTURE_MIN_FILTER, desc_.mipmaps && !integer ? GL_LINEAR_MIPMAP_LINEAR : filt);
        glTexParameteri(tgt, GL_TEXTURE_MAG_FILTER, filt);
        GLint wrap = desc_.clampToEdge ? GL_CLAMP_TO_EDGE : GL_REPEAT;
        glTexParameteri(tgt, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri(tgt, GL_TEXTURE_WRAP_T, wrap);
        if (desc_.mipmaps && pixels) glGenerateMipmap(tgt);
    }
    glBindTexture(tgt, 0);
}
void Texture2D::release() { if (id_) { glDeleteTextures(1, &id_); id_ = 0; } }
void Texture2D::bind(GLuint unit) const { glActiveTexture(GL_TEXTURE0 + unit); glBindTexture(target(), id_); }
void Texture2D::upload(const void* pixels) {
    GLenum internal, format, type;
    glFormat(desc_.format, internal, format, type);
    glBindTexture(GL_TEXTURE_2D, id_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, desc_.width, desc_.height, format, type, pixels);
    if (desc_.mipmaps) glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}
void Texture2D::resize(int w, int h) {
    if (w == desc_.width && h == desc_.height) return;
    desc_.width = w; desc_.height = h;
    allocate(nullptr);
}

// ---------------------------------------------------------------- TextureCube
TextureCube::TextureCube(const CubeDesc& d) : desc_(d) {
    desc_.levels = std::max(1, d.levels);
    GLenum internal, format, type;
    Texture2D::glFormat(desc_.format, internal, format, type);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);   // GL 3.2+: filter across face edges (no seams in mips)
    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_CUBE_MAP, id_);
    for (int level = 0; level < desc_.levels; ++level) {
        const int sz = levelSize(level);
        for (int face = 0; face < 6; ++face)
            glTexImage2D(faceTarget(face), level, static_cast<GLint>(internal), sz, sz, 0, format, type, nullptr);
    }
    const GLint filt = desc_.linear ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
                    desc_.levels > 1 ? (desc_.linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST) : filt);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, filt);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, desc_.levels - 1);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}
void TextureCube::release() { if (id_) { glDeleteTextures(1, &id_); id_ = 0; } }
void TextureCube::bind(GLuint unit) const { glActiveTexture(GL_TEXTURE0 + unit); glBindTexture(GL_TEXTURE_CUBE_MAP, id_); }
void TextureCube::generateMipmaps() {
    glBindTexture(GL_TEXTURE_CUBE_MAP, id_);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}
std::vector<float> TextureCube::readFace(int face, int level) const {
    const int sz = levelSize(level);
    std::vector<float> px(static_cast<std::size_t>(sz) * static_cast<std::size_t>(sz) * 4u, 0.0f);
    glBindTexture(GL_TEXTURE_CUBE_MAP, id_);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(faceTarget(face), level, GL_RGBA, GL_FLOAT, px.data());
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    return px;
}

// ---------------------------------------------------------------- TimerQuery
TimerQuery::TimerQuery(bool create) { if (create) glGenQueries(1, &id_); }
void TimerQuery::release() { if (id_) { glDeleteQueries(1, &id_); id_ = 0; } }
void TimerQuery::begin() { if (id_) glBeginQuery(GL_TIME_ELAPSED, id_); }
void TimerQuery::end() { if (id_) { glEndQuery(GL_TIME_ELAPSED); pending_ = true; } }
double TimerQuery::resultMs() {
    if (id_ && pending_) {
        GLint avail = 0;
        glGetQueryObjectiv(id_, GL_QUERY_RESULT_AVAILABLE, &avail);
        if (avail) {
            GLuint64 ns = 0;
            glGetQueryObjectui64v(id_, GL_QUERY_RESULT, &ns);
            lastMs_ = static_cast<double>(ns) * 1e-6;
            pending_ = false;
        }
    }
    return lastMs_;
}

// ---------------------------------------------------------------- Texture1D
Texture1D::Texture1D(std::span<const float> rgb, int count) {
    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_1D, id_);
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB32F, count, 0, GL_RGB, GL_FLOAT, rgb.data());
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_1D, 0);
}
void Texture1D::release() { if (id_) { glDeleteTextures(1, &id_); id_ = 0; } }
void Texture1D::bind(GLuint unit) const { glActiveTexture(GL_TEXTURE0 + unit); glBindTexture(GL_TEXTURE_1D, id_); }

// ---------------------------------------------------------------- Sampler
Sampler::Sampler(bool linear, bool clamp, bool compareDepth) {
    glGenSamplers(1, &id_);
    glSamplerParameteri(id_, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glSamplerParameteri(id_, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glSamplerParameteri(id_, GL_TEXTURE_WRAP_S, clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    glSamplerParameteri(id_, GL_TEXTURE_WRAP_T, clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    if (compareDepth) {
        glSamplerParameteri(id_, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(id_, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    }
}
void Sampler::release() { if (id_) { glDeleteSamplers(1, &id_); id_ = 0; } }
void Sampler::bind(GLuint unit) const { glBindSampler(unit, id_); }

// ---------------------------------------------------------------- Framebuffer
Framebuffer::Framebuffer() { glGenFramebuffers(1, &id_); }
void Framebuffer::release() { if (id_) { glDeleteFramebuffers(1, &id_); id_ = 0; } }
void Framebuffer::bind(GLenum target) const { glBindFramebuffer(target, id_); }
void Framebuffer::bindDefault(GLenum target) { glBindFramebuffer(target, 0); }
void Framebuffer::attachColor(int index, const Texture2D& tex) {
    glBindFramebuffer(GL_FRAMEBUFFER, id_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(index), tex.target(), tex.id(), 0);
}
void Framebuffer::attachDepth(const Texture2D& tex, bool stencil) {
    glBindFramebuffer(GL_FRAMEBUFFER, id_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT, tex.target(), tex.id(), 0);
}
void Framebuffer::attachCubeFace(int index, const TextureCube& cube, int face, int level) {
    glBindFramebuffer(GL_FRAMEBUFFER, id_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(index),
                           TextureCube::faceTarget(face), cube.id(), level);
}
void Framebuffer::setDrawBuffers(int count) {
    glBindFramebuffer(GL_FRAMEBUFFER, id_);
    GLenum bufs[8];
    for (int i = 0; i < count && i < 8; ++i) bufs[i] = GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i);
    if (count == 0) glDrawBuffer(GL_NONE); else glDrawBuffers(count, bufs);
}
Status Framebuffer::check() const {
    glBindFramebuffer(GL_FRAMEBUFFER, id_);
    GLenum s = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (s != GL_FRAMEBUFFER_COMPLETE) return fail(ErrorCode::Gfx_ + 10, "framebuffer incomplete: 0x" + std::to_string(s));
    return {};
}
void Framebuffer::blitTo(const Framebuffer& dst, int srcIndex, int dstIndex, int w, int h, GLenum filter, bool depth) const {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, id_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst.id());
    glReadBuffer(GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(srcIndex));
    GLenum db = GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(dstIndex);
    glDrawBuffers(1, &db);
    GLbitfield mask = GL_COLOR_BUFFER_BIT | (depth ? GL_DEPTH_BUFFER_BIT : 0u);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, mask, depth ? GL_NEAREST : filter);
}
void Framebuffer::blitToDefault(int srcIndex, int srcW, int srcH, int dstW, int dstH) const {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, id_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(srcIndex));
    glDrawBuffer(GL_BACK);
    glBlitFramebuffer(0, 0, srcW, srcH, 0, 0, dstW, dstH, GL_COLOR_BUFFER_BIT, GL_LINEAR);
}

} // namespace qlab::gfx
