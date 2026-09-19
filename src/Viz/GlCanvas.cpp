// Spec 21 §1.2 — GL backend and per-view canvas, built on gfx only (see GlCanvas.hpp).
#include "Viz/GlCanvas.hpp"
#include "Viz/Math/Color.hpp"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace qlab::viz {
namespace {

// Fixed key light of the baked shading, in world axes (upper right, toward the viewer).
const glm::vec3 kBakedLight = glm::normalize(glm::vec3(0.35f, 0.75f, 0.55f));

// Shading factor in scene-linear space: 1 where the surface faces the light, `floor` in shadow.
void bakeShading(gfx::MeshData& mesh, float floor) {
    for (auto& v : mesh.vertices) {
        const float lambert = std::max(0.0f, glm::dot(glm::normalize(v.normal), kBakedLight));
        const float shade = floor + (1.0f - floor) * lambert;
        v.color = glm::vec4(shade, shade, shade, 1.0f);
    }
}

gfx::MeshData liftedToBase(gfx::MeshData mesh) {
    mesh.transform(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.5f, 0.0f)));
    return mesh;
}

gfx::MeshData makeDisc(int segments) {
    gfx::MeshData m;
    m.vertices.push_back({{0, 0, 0}, {0, 1, 0}, {0.5f, 0.5f}});
    for (int i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments) * 6.2831853f;
        m.vertices.push_back({{std::cos(t), 0, std::sin(t)},
                              {0, 1, 0},
                              {0.5f + 0.5f * std::cos(t), 0.5f + 0.5f * std::sin(t)}});
    }
    for (int i = 0; i < segments; ++i)
        m.indices.insert(m.indices.end(), {0u, static_cast<std::uint32_t>(2 + i),
                                           static_cast<std::uint32_t>(1 + i)});
    m.orientToNormals();
    return m;
}

} // namespace

GlBackend::~GlBackend() = default;

Result<std::unique_ptr<GlBackend>> GlBackend::create(const GlBackendDesc& desc) {
    auto backend = std::unique_ptr<GlBackend>(new GlBackend());
    backend->desc_ = desc;
    gfx::RendererDesc rd;
    rd.samples = std::max(1, desc.samples);
    rd.shadowSize = 16; // nothing in a view casts or receives shadows; the map only has to exist
    rd.exposure = 1.0f;
    // The state views are diagrams with a baked key light and exact display colours (spec 21
    // §1.2): no environment reflections, no ambient occlusion, no bloom — those belong to the lab.
    rd.ibl = false;
    rd.ssao = false;
    rd.bloom = false;
    rd.clearColor =
        math::displayToScene(desc.background); // so the canvas matches the panel exactly
    QXL_TRY_ASSIGN(backend->renderer_, gfx::Renderer::create(rd));
    // Soft, frontal lighting for the few lit meshes (arrows): shape cues without deep shadows.
    backend->renderer_->setSun(-kBakedLight, glm::vec3(1.0f), 2.2f);
    backend->renderer_->setAmbient(glm::vec3(0.55f));

    gfx::MeshData sphere = gfx::shapes::sphere(1.0f, 48, 24);
    bakeShading(sphere, 0.55f);
    backend->sphere_.upload(sphere);
    gfx::MeshData low = gfx::shapes::sphere(1.0f, 12, 8);
    bakeShading(low, 0.55f);
    backend->sphereLow_.upload(low);
    gfx::MeshData box = liftedToBase(gfx::shapes::box(glm::vec3(1.0f)));
    bakeShading(box, 0.45f);
    backend->box_.upload(box);
    backend->disc_.upload(makeDisc(48));
    backend->cylinder_.upload(liftedToBase(gfx::shapes::cylinder(1.0f, 1.0f, 24, true)));
    backend->cone_.upload(liftedToBase(gfx::shapes::cone(1.0f, 1.0f, 24)));
    return backend;
}

glm::vec4 GlBackend::exact(const glm::vec4& srgb) {
    return math::displayToScene(srgb);
}
glm::vec4 GlBackend::exact(const glm::vec3& srgb, float alpha) {
    return glm::vec4(math::displayToScene(srgb), alpha);
}

Status GlCanvas::ensureTarget(int w, int h) {
    if (w == width_ && h == height_ && fbo_)
        return {};
    gfx::TexDesc desc;
    desc.width = w;
    desc.height = h;
    desc.format = gfx::TexFormat::RGBA8;
    desc.linear = true;
    color_ = gfx::Texture2D(desc);
    fbo_.emplace();
    fbo_->attachColor(0, color_);
    fbo_->setDrawBuffers(1);
    QXL_TRY(fbo_->check());
    gfx::Framebuffer::bindDefault();
    width_ = w;
    height_ = h;
    dirty_ = true;
    return {};
}

Result<bool> GlCanvas::render(GlBackend& gl, int widthPx, int heightPx, const SceneFn& scene,
                              double timeS) {
    const int w = std::clamp(widthPx, 1, 8192), h = std::clamp(heightPx, 1, 8192);
    QXL_TRY(ensureTarget(w, h));
    if (!dirty_)
        return false;
    gfx::Renderer& r = gl.renderer();
    camera_.setAspect(static_cast<double>(w) / static_cast<double>(h));
    r.beginFrame(w, h, camera_, timeS);
    r.setSelection(ComponentId{0}, ComponentId{0}); // hit testing is done on the CPU; no id outline
    if (scene)
        scene(r, gl);
    r.endFrame();
    // The renderer's output is shared by every canvas: keep a copy of this view's image.
    fbo_->bind();
    r.blitToScreen(0, 0, w, h);
    gfx::Framebuffer::bindDefault();
    dirty_ = false;
    return true;
}

Status GlCanvas::renderToPng(GlBackend& gl, int widthPx, int heightPx, const SceneFn& scene,
                             const std::filesystem::path& png) {
    dirty_ = true;
    QXL_TRY(render(gl, widthPx, heightPx, scene, 0.0));
    return gl.renderer().screenshot(png); // the renderer still holds this canvas's frame
}

glm::mat4 alignY(const glm::dvec3& a, const glm::dvec3& b, double radius) {
    const glm::dvec3 d = b - a;
    const double len = glm::length(d);
    if (len < 1e-12)
        return glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(a)), glm::vec3(0.0f));
    const glm::dvec3 y = d / len;
    const glm::dvec3 ref = std::abs(y.y) < 0.99 ? glm::dvec3(0, 1, 0) : glm::dvec3(1, 0, 0);
    const glm::dvec3 x = glm::normalize(glm::cross(ref, y));
    const glm::dvec3 z = glm::cross(x, y);
    glm::dmat4 m(1.0);
    m[0] = glm::dvec4(x * radius, 0.0);
    m[1] = glm::dvec4(y * len, 0.0);
    m[2] = glm::dvec4(z * radius, 0.0);
    m[3] = glm::dvec4(a, 1.0);
    return glm::mat4(m);
}

} // namespace qlab::viz
