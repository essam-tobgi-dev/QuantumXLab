#pragma once
// Spec 18 §2 — RAII wrappers for GL objects. Every GL handle is owned by exactly one wrapper;
// move-only. GL 4.1 core (no DSA): wrappers bind internally where needed.
#include "Core/Error.hpp"
#include "Graphics/GlLoader.hpp"
#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace qlab::gfx {

// Base move-only handle.
class GlHandle {
public:
    GlHandle() = default;
    GlHandle(const GlHandle&) = delete;
    GlHandle& operator=(const GlHandle&) = delete;
    GlHandle(GlHandle&& o) noexcept : id_(std::exchange(o.id_, 0)) {}
    GlHandle& operator=(GlHandle&& o) noexcept { if (this != &o) { release(); id_ = std::exchange(o.id_, 0); } return *this; }
    virtual ~GlHandle() = default;
    GLuint id() const { return id_; }
    explicit operator bool() const { return id_ != 0; }
protected:
    virtual void release() {}
    GLuint id_ = 0;
};

enum class BufferUsage { Static, Dynamic, Stream };

class Buffer : public GlHandle {
public:
    Buffer() = default;
    explicit Buffer(GLenum target);
    ~Buffer() override { release(); }
    Buffer(Buffer&&) = default;
    Buffer& operator=(Buffer&&) = default;
    void bind() const;
    void bindBase(GLuint index) const; // for UBOs
    void setData(std::span<const std::byte> bytes, BufferUsage usage = BufferUsage::Static);
    template <class T> void setData(std::span<const T> v, BufferUsage u = BufferUsage::Static) {
        setData(std::as_bytes(v), u);
    }
    template <class T> void setData(const std::vector<T>& v, BufferUsage u = BufferUsage::Static) {
        setData(std::span<const T>(v), u);
    }
    void update(std::size_t offset, std::span<const std::byte> bytes); // glBufferSubData
    std::size_t sizeBytes() const { return size_; }
    GLenum target() const { return target_; }
protected:
    void release() override;
private:
    GLenum target_ = GL_ARRAY_BUFFER;
    std::size_t size_ = 0;
};

struct VertexAttrib {
    GLuint index; GLint components; GLenum type; bool normalized; GLsizei stride; std::size_t offset;
    GLuint divisor = 0; // 1 = per-instance
};

class VertexArray : public GlHandle {
public:
    VertexArray();
    ~VertexArray() override { release(); }
    VertexArray(VertexArray&&) = default;
    VertexArray& operator=(VertexArray&&) = default;
    void bind() const;
    static void unbind();
    // Binds `buf` as ARRAY_BUFFER and sets the attributes on this VAO.
    void setAttribs(const Buffer& buf, std::span<const VertexAttrib> attribs);
    void setIndexBuffer(const Buffer& ibo);
protected:
    void release() override;
};

enum class TexFormat { RGBA8, SRGBA8, RGBA16F, RGB16F, R11G11B10F, R32UI, R8, RG16F, R32F, Depth24Stencil8, Depth32F };

struct TexDesc {
    int width = 1, height = 1;
    TexFormat format = TexFormat::RGBA8;
    int samples = 0;          // >0 = multisample texture
    bool mipmaps = false;
    bool linear = true;       // filtering (ignored for integer / multisample)
    bool clampToEdge = true;
};

class Texture2D : public GlHandle {
public:
    Texture2D() = default;
    explicit Texture2D(const TexDesc& d, const void* pixels = nullptr);
    ~Texture2D() override { release(); }
    Texture2D(Texture2D&&) = default;
    Texture2D& operator=(Texture2D&&) = default;
    void bind(GLuint unit) const;
    GLenum target() const { return desc_.samples > 0 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D; }
    const TexDesc& desc() const { return desc_; }
    int width() const { return desc_.width; }
    int height() const { return desc_.height; }
    void upload(const void* pixels); // full re-upload, non-MSAA only
    void resize(int w, int h);
    static void glFormat(TexFormat f, GLenum& internal, GLenum& format, GLenum& type);
protected:
    void release() override;
private:
    void allocate(const void* pixels);
    TexDesc desc_;
};

// Cubemap (spec 18 §4 IBL): `levels` mip levels allocated up front (level k is size >> k), each
// face/level attachable to a Framebuffer for GPU generation. Seamless filtering is enabled
// context-wide on construction. Formats: RGB16F / RGBA16F / RGBA8.
struct CubeDesc {
    int size = 256;
    TexFormat format = TexFormat::RGB16F;
    int levels = 1;           // 1 = no mip chain
    bool linear = true;
};
class TextureCube : public GlHandle {
public:
    TextureCube() = default;
    explicit TextureCube(const CubeDesc& d);
    ~TextureCube() override { release(); }
    TextureCube(TextureCube&&) = default;
    TextureCube& operator=(TextureCube&&) = default;
    void bind(GLuint unit) const;
    const CubeDesc& desc() const { return desc_; }
    int size() const { return desc_.size; }
    int levels() const { return desc_.levels; }
    int levelSize(int level) const { return std::max(1, desc_.size >> level); }
    void generateMipmaps();   // fills levels 1..n-1 from level 0 (box filter)
    // Reads one face level back as RGBA floats (row-major, levelSize² × 4). Synchronous.
    std::vector<float> readFace(int face, int level) const;
    static GLenum faceTarget(int face) { return GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<GLenum>(face); }
protected:
    void release() override;
private:
    CubeDesc desc_;
};

// GPU elapsed-time query (spec 18 §11). `begin`/`end` bracket GL work (GL_TIME_ELAPSED queries
// cannot nest, so consecutive segments are timed and summed); `resultMs` hands back the elapsed
// time once it is available — a frame later in practice — and the previous value while the
// query is in flight, so reading never stalls the pipeline. GL_TIMESTAMP is not usable here:
// Apple's GL 4.1 answers glQueryCounter with 0.
class TimerQuery : public GlHandle {
public:
    TimerQuery() = default;
    explicit TimerQuery(bool create);
    ~TimerQuery() override { release(); }
    TimerQuery(TimerQuery&&) = default;
    TimerQuery& operator=(TimerQuery&&) = default;
    void begin();
    void end();
    double resultMs();
protected:
    void release() override;
private:
    bool pending_ = false;
    double lastMs_ = 0.0;
};

// 1D texture for colormaps (spec 18 §6).
class Texture1D : public GlHandle {
public:
    Texture1D() = default;
    Texture1D(std::span<const float> rgb, int count); // rgb triplets
    ~Texture1D() override { release(); }
    Texture1D(Texture1D&&) = default;
    Texture1D& operator=(Texture1D&&) = default;
    void bind(GLuint unit) const;
protected:
    void release() override;
};

class Sampler : public GlHandle {
public:
    Sampler() = default;
    Sampler(bool linear, bool clamp, bool compareDepth = false);
    ~Sampler() override { release(); }
    Sampler(Sampler&&) = default;
    Sampler& operator=(Sampler&&) = default;
    void bind(GLuint unit) const;
protected:
    void release() override;
};

class Framebuffer : public GlHandle {
public:
    Framebuffer();
    ~Framebuffer() override { release(); }
    Framebuffer(Framebuffer&&) = default;
    Framebuffer& operator=(Framebuffer&&) = default;
    void bind(GLenum target = GL_FRAMEBUFFER) const;
    static void bindDefault(GLenum target = GL_FRAMEBUFFER);
    void attachColor(int index, const Texture2D& tex);
    void attachDepth(const Texture2D& tex, bool stencil);
    // Attach one face and mip level of a cubemap as colour attachment `index` (IBL generation).
    void attachCubeFace(int index, const TextureCube& cube, int face, int level);
    void setDrawBuffers(int count);
    Status check() const;
    // Blit colour attachment `index` (and optionally depth) of this into dst (nearest for integer).
    void blitTo(const Framebuffer& dst, int srcIndex, int dstIndex, int w, int h, GLenum filter,
                bool depth = false) const;
    void blitToDefault(int srcIndex, int srcW, int srcH, int dstW, int dstH) const;
protected:
    void release() override;
};

} // namespace qlab::gfx
