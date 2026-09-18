#include "Core/Log.hpp"
#include "Core/Paths.hpp"
#include "Graphics/GlCheck.hpp"
#include "Graphics/Renderer.hpp"

namespace qlab::gfx {

const Mesh* LodGroup::select(float dist) const {
    if (meshes.empty()) return nullptr;
    for (std::size_t i = 0; i < distances.size() && i + 1 < meshes.size(); ++i)
        if (dist < distances[i]) return meshes[i];
    return meshes.back();
}

Renderer::~Renderer() = default;

Result<std::unique_ptr<Renderer>> Renderer::create(const RendererDesc& d) {
    auto r = std::unique_ptr<Renderer>(new Renderer());
    if (auto s = r->init(d); !s) return std::unexpected(s.error());
    return r;
}

Status Renderer::init(const RendererDesc& d) {
    desc_ = d;
    auto load = [&](ShaderProgram& p, const char* name, const char* vs, const char* fs, std::vector<std::string> defs = {}) -> Status {
        ShaderDesc sd; sd.name = name; sd.vertex = vs; sd.fragment = fs; sd.defines = std::move(defs);
        auto r = ShaderProgram::fromFiles(sd);
        if (!r) return std::unexpected(r.error());
        p = std::move(*r);
        p.bindUniformBlock("FrameUbo", kUboFrame);
        p.bindUniformBlock("LightsUbo", kUboLights);
        p.bindUniformBlock("MaterialUbo", kUboMaterial);
        p.bindUniformBlock("ObjectUbo", kUboObject);
        p.bindUniformBlock("SelectionUbo", kUboSelection);
        return {};
    };
    QXL_TRY(load(pbr_, "pbr", "pbr.vert", "pbr.frag"));
    QXL_TRY(load(pbrInst_, "pbr_instanced", "pbr.vert", "pbr.frag", {"INSTANCED"}));
    QXL_TRY(load(shadow_, "shadow", "shadow.vert", "shadow.frag"));
    QXL_TRY(load(shadowInst_, "shadow_instanced", "shadow.vert", "shadow.frag", {"INSTANCED"}));
    QXL_TRY(load(idProg_, "id", "pbr.vert", "id.frag"));
    QXL_TRY(load(idInst_, "id_instanced", "pbr.vert", "id.frag", {"INSTANCED"}));
    QXL_TRY(load(line_, "line", "line.vert", "line.frag"));
    QXL_TRY(load(textSdf_, "text_sdf", "text_sdf.vert", "text_sdf.frag"));
    QXL_TRY(load(postProg_, "post", "fullscreen.vert", "post.frag"));
    QXL_TRY(load(outline_, "outline", "fullscreen.vert", "outline.frag"));
    QXL_TRY(load(envLab_, "env_lab", "cube_face.vert", "env_lab.frag"));
    QXL_TRY(load(envPrefilter_, "env_prefilter", "cube_face.vert", "env_prefilter.frag"));
    QXL_TRY(load(envIrrProg_, "env_irradiance", "cube_face.vert", "env_irradiance.frag"));
    QXL_TRY(load(brdfLutProg_, "brdf_lut", "fullscreen.vert", "brdf_lut.frag"));
    QXL_TRY(load(ssaoProg_, "ssao", "fullscreen.vert", "ssao.frag"));
    QXL_TRY(load(ssaoBlur_, "ssao_blur", "fullscreen.vert", "ssao_blur.frag"));
    QXL_TRY(load(ssaoUpsample_, "ssao_blur_upsample", "fullscreen.vert", "ssao_blur.frag", {"UPSAMPLE"}));
    QXL_TRY(load(bloomBright_, "bloom_bright", "fullscreen.vert", "bloom_blur.frag", {"BRIGHT"}));
    QXL_TRY(load(bloomBlur_, "bloom_blur", "fullscreen.vert", "bloom_blur.frag"));

    auto mkUbo = [](Buffer& b, std::size_t bytes, GLuint binding) {
        b = Buffer(GL_UNIFORM_BUFFER);
        std::vector<std::byte> z(bytes);
        b.setData(std::span<const std::byte>(z), BufferUsage::Dynamic);
        b.bindBase(binding);
    };
    mkUbo(uboFrame_, sizeof(FrameUbo), kUboFrame);
    mkUbo(uboLights_, sizeof(LightsUbo), kUboLights);
    mkUbo(uboMaterial_, sizeof(MaterialUbo), kUboMaterial);
    mkUbo(uboObject_, sizeof(ObjectUbo), kUboObject);
    mkUbo(uboSelection_, sizeof(SelectionUbo), kUboSelection);

    // shadow map
    TexDesc sd; sd.width = sd.height = d.shadowSize; sd.format = TexFormat::Depth32F; sd.linear = true;
    shadowDepth_ = Texture2D(sd);
    shadowFbo_ = Framebuffer();
    shadowFbo_.attachDepth(shadowDepth_, false);
    shadowFbo_.setDrawBuffers(0);
    QXL_TRY(shadowFbo_.check());
    shadowSampler_ = Sampler(true, true, true);
    Framebuffer::bindDefault();

    // fullscreen quad
    MeshData q;
    q.vertices = {{{-1, -1, 0}, {0, 0, 1}, {0, 0}}, {{1, -1, 0}, {0, 0, 1}, {1, 0}}, {{1, 1, 0}, {0, 0, 1}, {1, 1}}, {{-1, 1, 0}, {0, 0, 1}, {0, 1}}};
    q.indices = {0, 1, 2, 0, 2, 3};
    screenQuad_.upload(q);
    instBuf_ = Buffer(GL_ARRAY_BUFFER);
    for (auto& tq : postQuery_) tq = TimerQuery(true);
    // Spec 18 §5 EnvPrefilter: the IBL resources are built even when `ibl` is off so it can be
    // toggled at run time (they cost < 150 ms once and a few MB).
    QXL_TRY(buildEnvironment());

    lights_.sun.direction = glm::vec4(glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f)), 0);
    lights_.sun.color = glm::vec4(1.0f, 0.97f, 0.92f, 3.0f);
    lights_.ambient = glm::vec4(0.18f, 0.2f, 0.24f, 0.0f);
    sel_.outlineColor = glm::vec4(1.0f, 0.75f, 0.2f, 1.0f);
    sel_.hoverColor = glm::vec4(0.4f, 0.8f, 1.0f, 1.0f);
    sel_.params = glm::vec4(2.0f, 0, 0, 0);

    auto ttf = d.fontTtf.empty() ? core::assetDir() / "Fonts" / "Inter-Regular.ttf" : d.fontTtf;
    auto f = SdfFont::load(ttf, 48);
    if (!f) QXL_LOG_WARN(Gfx, "font not loaded ({}); text disabled", f.error().message);
    else { font_ = std::move(*f); text_.setFont(font_.get()); }
    cmaps_.ensure();
    resize(16, 16);
    drainGlErrors("Renderer::init");
    return {};
}

void Renderer::resize(int w, int h) {
    if (w == w_ && h == h_) return;
    w_ = std::max(1, w); h_ = std::max(1, h);
    int s = desc_.samples;
    TexDesc c; c.width = w_; c.height = h_; c.format = TexFormat::RGBA16F; c.samples = s;
    TexDesc dd = c; dd.format = TexFormat::Depth24Stencil8;
    msaaColor_ = Texture2D(c); msaaDepth_ = Texture2D(dd);
    msaaFbo_ = Framebuffer();
    msaaFbo_.attachColor(0, msaaColor_); msaaFbo_.attachDepth(msaaDepth_, true);
    msaaFbo_.setDrawBuffers(1);
    if (auto st = msaaFbo_.check(); !st) QXL_LOG_ERROR(Gfx, "msaa fbo: {}", st.error().message);
    c.samples = 0; dd.samples = 0;
    resColor_ = Texture2D(c);
    resolveFbo_ = Framebuffer();
    resolveFbo_.attachColor(0, resColor_);
    resolveFbo_.setDrawBuffers(1);
    if (auto st = resolveFbo_.check(); !st) QXL_LOG_ERROR(Gfx, "resolve fbo: {}", st.error().message);
    TexDesc i = c; i.format = TexFormat::R32UI; i.linear = false;
    resId_ = Texture2D(i); idDepth_ = Texture2D(dd);
    TexDesc ld = c; ld.format = TexFormat::R32F; ld.linear = false;
    idLinDepth_ = Texture2D(ld);
    idFbo_ = Framebuffer();
    idFbo_.attachColor(0, resId_); idFbo_.attachColor(1, idLinDepth_); idFbo_.attachDepth(idDepth_, true);
    idFbo_.setDrawBuffers(2);
    if (auto st = idFbo_.check(); !st) QXL_LOG_ERROR(Gfx, "id fbo: {}", st.error().message);
    TexDesc p; p.width = w_; p.height = h_; p.format = TexFormat::RGBA8;
    postTex_ = Texture2D(p);
    postFbo_ = Framebuffer();
    postFbo_.attachColor(0, postTex_);
    postFbo_.setDrawBuffers(1);
    if (auto st = postFbo_.check(); !st) QXL_LOG_ERROR(Gfx, "post fbo: {}", st.error().message);
    // SSAO: raw and horizontally blurred (ao, depth) at half resolution, R8 result at full.
    const int hw = std::max(1, w_ / 2), hh = std::max(1, h_ / 2);
    TexDesc ao; ao.width = hw; ao.height = hh; ao.format = TexFormat::RG16F; ao.linear = true;
    ssaoRaw_ = Texture2D(ao); ssaoH_ = Texture2D(ao);
    ssaoRawFbo_ = Framebuffer(); ssaoRawFbo_.attachColor(0, ssaoRaw_); ssaoRawFbo_.setDrawBuffers(1);
    if (auto st = ssaoRawFbo_.check(); !st) QXL_LOG_ERROR(Gfx, "ssao raw fbo: {}", st.error().message);
    ssaoHFbo_ = Framebuffer(); ssaoHFbo_.attachColor(0, ssaoH_); ssaoHFbo_.setDrawBuffers(1);
    if (auto st = ssaoHFbo_.check(); !st) QXL_LOG_ERROR(Gfx, "ssao h fbo: {}", st.error().message);
    TexDesc aoFull; aoFull.width = w_; aoFull.height = h_; aoFull.format = TexFormat::R8; aoFull.linear = true;
    ssaoTex_ = Texture2D(aoFull);
    ssaoFbo_ = Framebuffer(); ssaoFbo_.attachColor(0, ssaoTex_); ssaoFbo_.setDrawBuffers(1);
    if (auto st = ssaoFbo_.check(); !st) QXL_LOG_ERROR(Gfx, "ssao fbo: {}", st.error().message);
    // Bloom: two half-resolution ping-pong targets; packed 11/11/10 float halves the bandwidth of
    // RGBA16F (the blur is bandwidth-bound at 3200×2000) and still carries HDR radiance.
    TexDesc bl; bl.width = hw; bl.height = hh; bl.format = TexFormat::R11G11B10F; bl.linear = true;
    bloomA_ = Texture2D(bl); bloomB_ = Texture2D(bl);
    bloomFboA_ = Framebuffer(); bloomFboA_.attachColor(0, bloomA_); bloomFboA_.setDrawBuffers(1);
    if (auto st = bloomFboA_.check(); !st) QXL_LOG_ERROR(Gfx, "bloom fbo A: {}", st.error().message);
    bloomFboB_ = Framebuffer(); bloomFboB_.attachColor(0, bloomB_); bloomFboB_.setDrawBuffers(1);
    if (auto st = bloomFboB_.check(); !st) QXL_LOG_ERROR(Gfx, "bloom fbo B: {}", st.error().message);
    Framebuffer::bindDefault();
    idReadback_.assign(static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_), 0);
}

} // namespace qlab::gfx
