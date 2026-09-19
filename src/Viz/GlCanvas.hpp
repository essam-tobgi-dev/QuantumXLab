#pragma once
// Spec 21 §1.2 — the GL drawing backend of the 3D and many-primitive views. Nothing here calls
// OpenGL directly (spec 18: only `gfx` does): a canvas draws through `gfx::Renderer` — cameras,
// meshes, WORLD-space line and SDF-text batches, 4× MSAA, tone mapping — and keeps the finished
// image in a framebuffer of its own, shown as an ImGui image.
//
//   GlBackend  one per GL context, owned by the host: the renderer every canvas draws through
//              (its MSAA targets follow the size of the canvas being drawn) and the unit meshes.
//   GlCanvas   one per view instance: an RGBA8 texture + framebuffer at the panel's pixel size ×
//              DPI scale, a camera, and a dirty flag so an unchanged view costs nothing per frame.
#include "Core/Error.hpp"
#include "Graphics/Camera.hpp"
#include "Graphics/GlObjects.hpp"
#include "Graphics/Mesh.hpp"
#include "Graphics/Renderer.hpp"
#include <filesystem>
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <optional>

namespace qlab::viz {

struct GlBackendDesc {
    glm::vec3 background{0.086f, 0.102f,
                         0.125f}; // display colour behind every canvas (theme bg.panel)
    int samples = 4;              // spec 21 §1.2: 4× MSAA
};

// Why ONE renderer for all canvases rather than one per view: `gfx::Renderer` binds its five UBOs
// to the context-global binding points 0–4 only in `init()`, so with two renderers alive the older
// one draws with the newer one's frame/object/material uniforms. Measured here: the same unlit
// sphere reads 79,163,255 at the centre with one renderer and 66,146,254 once a second one has
// been created and used. Every canvas therefore shares the backend's renderer and keeps a copy of
// its image. The same defect applies between this backend and the lab viewport's renderer — that
// has to be fixed in Graphics (re-`bindBase` the UBOs at the start of each frame), not here.
class GlBackend {
  public:
    // Needs a current GL context (the host's window, or a hidden one in tests). Create one per
    // context; re-create it when the theme's panel colour changes (the clear colour is fixed).
    static Result<std::unique_ptr<GlBackend>> create(const GlBackendDesc& desc = {});
    ~GlBackend();

    gfx::Renderer& renderer() { return *renderer_; }
    const GlBackendDesc& desc() const { return desc_; }

    // Unit meshes. `sphere`, `box` and `disc` carry baked directional shading in their vertex
    // colours and are drawn unlit, so a phase hue or a colormap value reaches the screen exactly
    // (math::displayToScene undoes the renderer's tone map). `cylinder` and `cone` are lit.
    const gfx::Mesh& sphere() const { return sphere_; } // radius 1
    const gfx::Mesh& sphereLow() const {
        return sphereLow_;
    } // radius 1, few triangles (many nodes)
    const gfx::Mesh& box() const {
        return box_;
    } // unit cube, base at y = 0: x,z ∈ [−½, ½], y ∈ [0, 1]
    const gfx::Mesh& disc() const { return disc_; }         // radius 1 in the xz plane, facing +y
    const gfx::Mesh& cylinder() const { return cylinder_; } // radius 1, y ∈ [0, 1]
    const gfx::Mesh& cone() const { return cone_; }         // base radius 1 at y = 0, apex at y = 1

    // Scene-linear colour that the renderer shows as the display colour `srgb`.
    static glm::vec4 exact(const glm::vec4& srgb);
    static glm::vec4 exact(const glm::vec3& srgb, float alpha = 1.0f);

  private:
    GlBackend() = default;
    GlBackendDesc desc_;
    std::unique_ptr<gfx::Renderer> renderer_;
    gfx::Mesh sphere_, sphereLow_, box_, disc_, cylinder_, cone_;
};

class GlCanvas {
  public:
    // Submits the scene between the renderer's beginFrame and endFrame.
    using SceneFn = std::function<void(gfx::Renderer&, GlBackend&)>;

    gfx::Camera& camera() { return camera_; }
    const gfx::Camera& camera() const { return camera_; }

    // Marks the image out of date; `render` is skipped while the canvas is clean and its size is
    // unchanged (spec 24: an unchanged view costs nothing).
    void invalidate() { dirty_ = true; }
    bool dirty() const { return dirty_; }

    // Renders the scene at widthPx × heightPx FRAMEBUFFER pixels (panel size × DPI scale) and
    // copies the tone-mapped result into this canvas's texture. Returns whether it rendered.
    Result<bool> render(GlBackend& gl, int widthPx, int heightPx, const SceneFn& scene,
                        double timeS = 0.0);
    // Renders unconditionally and writes the image as PNG (spec 21 §3.13 export, 23 §8).
    Status renderToPng(GlBackend& gl, int widthPx, int heightPx, const SceneFn& scene,
                       const std::filesystem::path& png);

    bool hasImage() const { return width_ > 0 && fbo_.has_value(); }
    int width() const { return width_; }
    int height() const { return height_; }
    // GL texture name for `ImGui::Image` (origin bottom-left: draw with uv0 = (0,1), uv1 = (1,0)).
    std::uint64_t textureId() const { return color_.id(); }
    // Precondition: hasImage(). (Tests read pixels back through it.)
    const gfx::Framebuffer& framebuffer() const { return *fbo_; }

  private:
    Status ensureTarget(int w, int h);
    gfx::Camera camera_;
    gfx::Texture2D color_;
    // Created on the first render: a gfx::Framebuffer generates its GL name in its constructor, and
    // a canvas is a member of every GL view, which must be constructible without a GL context
    // (headless tests, views created before the window exists).
    std::optional<gfx::Framebuffer> fbo_;
    int width_ = 0, height_ = 0;
    bool dirty_ = true;
};

// Quantum frame → renderer world frame. The Bloch and Q-spheres are drawn with z up (|0⟩ at the
// top), x toward the viewer and y to the right; gfx is y-up, so (x, y, z) ↦ (y, z, x) — a proper
// rotation, handedness preserved.
inline glm::dvec3 quantumToWorld(const glm::dvec3& q) {
    return {q.y, q.z, q.x};
}

// Model matrix that maps the unit +y axis onto the segment a → b with the given radius (for the
// `cylinder` and `cone` meshes).
glm::mat4 alignY(const glm::dvec3& a, const glm::dvec3& b, double radius);

} // namespace qlab::viz
