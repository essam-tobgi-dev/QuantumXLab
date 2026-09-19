#include "Graphics/Renderer.hpp"
#include "Core/Timer.hpp"
#include "Graphics/GlCheck.hpp"
#include <algorithm>
#include <cstdint>
#include <glm/gtc/matrix_transform.hpp>
#include <stb/stb_image_write.h>

namespace qlab::gfx {
namespace {
template <class T> void updateUbo(Buffer& b, const T& v) {
    b.update(0, std::span<const std::byte>(reinterpret_cast<const std::byte*>(&v), sizeof(T)));
}
} // namespace

void Renderer::beginFrame(int w, int h, const Camera& cam, double timeS) {
    // The uniform block binding points 0–4 (spec 18 §4) are context-global state, and `init()` is
    // not the only thing that writes them: a second renderer in the same context (the laboratory
    // viewport beside `viz::GlBackend`, spec 21 §1.2) binds ITS buffers there when it is created,
    // after which this one would draw with the other's frame, object and material uniforms. Each
    // renderer therefore re-claims the binding points at the start of its own frame.
    uboFrame_.bindBase(kUboFrame);
    uboLights_.bindBase(kUboLights);
    uboMaterial_.bindBase(kUboMaterial);
    uboObject_.bindBase(kUboObject);
    uboSelection_.bindBase(kUboSelection);
    resize(w, h);
    cam_ = cam;
    cam_.setAspect(static_cast<double>(w_) / h_);
    time_ = timeS;
    opaque_.clear();
    transparent_.clear();
    instanced_.clear();
    lines_.clear();
    linesNd_.clear();
    text_.clear();
    // Overlay batches take world-space positions and make them camera-relative in double precision.
    lines_.setOrigin(cam_.position());
    linesNd_.setOrigin(cam_.position());
    text_.setOrigin(cam_.position());
    stats_ = FrameStats{};
}

void Renderer::submit(const Mesh& m, const Material& mat, const glm::mat4& model, ComponentId id,
                      SubmitFlags f) {
    if (!m.valid())
        return;
    // frustum cull on the transformed AABB (camera-relative)
    Aabb box = m.bounds().transformed(glm::dmat4(model));
    if (!cam_.frustum().intersects(box))
        return;
    float depth = static_cast<float>(glm::length(box.center() - cam_.position()));
    Item it{&m, mat, model, id, f, depth};
    (mat.transparent() ? transparent_ : opaque_).push_back(it);
}
void Renderer::submitInstanced(const Mesh& m, const Material& mat,
                               std::span<const InstanceData> inst, SubmitFlags f) {
    if (!m.valid() || inst.empty())
        return;
    instanced_.push_back({&m, mat, std::vector<InstanceData>(inst.begin(), inst.end()), f});
}
void Renderer::setSelection(ComponentId s, ComponentId h) {
    sel_.selected = glm::uvec4(s.value, h.value, 0, 0);
}
void Renderer::setSun(glm::vec3 dir, glm::vec3 c, float i) {
    lights_.sun.direction = glm::vec4(glm::normalize(dir), 0);
    lights_.sun.color = glm::vec4(c, i);
}
void Renderer::setPointLights(std::span<const PointLight> pts) {
    int n = static_cast<int>(std::min<std::size_t>(pts.size(), kMaxPointLights));
    for (int i = 0; i < n; ++i)
        lights_.points[i] = pts[static_cast<std::size_t>(i)];
    lights_.ambient.w = static_cast<float>(n);
}

void Renderer::shadowPass() {
    core::Timer t;
    // Orthographic light box around the union of opaque bounds (camera-relative coordinates).
    Aabb scene{glm::dvec3(1e300), glm::dvec3(-1e300)};
    bool any = false;
    for (auto& it : opaque_)
        if (it.flags.castShadow) {
            scene.expand(it.mesh->bounds().transformed(glm::dmat4(it.model)).min);
            scene.expand(it.mesh->bounds().transformed(glm::dmat4(it.model)).max);
            any = true;
        }
    for (auto& ii : instanced_)
        if (ii.flags.castShadow)
            for (auto& d : ii.data) {
                Aabb b = ii.mesh->bounds().transformed(glm::dmat4(d.model));
                scene.expand(b.min);
                scene.expand(b.max);
                any = true;
            }
    glm::mat4 lvp(1.0f);
    double r = 1.0;
    if (any) {
        glm::dvec3 c = scene.center() - cam_.position();
        r = std::max(scene.radius(), 1e-3);
        glm::vec3 dir = glm::vec3(lights_.sun.direction);
        glm::vec3 eye = glm::vec3(c) - dir * static_cast<float>(r * 2.0);
        glm::vec3 up = std::abs(dir.y) > 0.95f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        glm::mat4 lv = glm::lookAt(eye, glm::vec3(c), up);
        float fr = static_cast<float>(r);
        glm::mat4 lp = glm::ortho(-fr, fr, -fr, fr, 0.01f, fr * 4.0f);
        lvp = lp * lv;
    }
    FrameUbo f{};
    f.view = cam_.viewRel();
    f.proj = cam_.projMatrix();
    f.viewProj = f.proj * f.view;
    f.lightViewProj = lvp;
    // xyz: the camera's world position (float) so the TEXTURED triplanar path can restore world
    // space from the camera-relative vWorld; every other shader works camera-relative.
    f.cameraPos = glm::vec4(glm::vec3(cam_.position()), static_cast<float>(time_));
    f.viewport = glm::vec4(w_, h_, 1.0f / w_, 1.0f / h_);
    // Soft shadows (spec 18 §4 amended): the ortho box spans 2r across and 4r in depth, so one
    // texel is 2r/S metres and a world offset δ is δ/(4r) in window depth. The lookup point is
    // pushed 1.5 texels along the normal (normal-offset bias) and compared with a 1-texel depth
    // bias; the 16-tap Poisson disc has radius 1.5 texels.
    const float texelWorld = static_cast<float>(2.0 * r / desc_.shadowSize);
    const float depthBias = 1.0f / (2.0f * static_cast<float>(desc_.shadowSize));
    f.params =
        glm::vec4(desc_.exposure, depthBias, 1.0f / desc_.shadowSize, cam_.ortho() ? 1.f : 0.f);
    f.shadow = glm::vec4(1.5f * texelWorld, 1.5f, texelWorld, 0.0f);
    f.post = glm::vec4(envIntensity_, desc_.ibl ? 1.0f : 0.0f, desc_.ssao ? desc_.aoStrength : 0.0f,
                       desc_.bloom ? desc_.bloomStrength : 0.0f);
    updateUbo(uboFrame_, f);
    updateUbo(uboLights_, lights_);
    updateUbo(uboSelection_, sel_);

    shadowFbo_.bind();
    glViewport(0, 0, desc_.shadowSize, desc_.shadowSize);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    if (any) {
        shadow_.use();
        for (auto& it : opaque_) {
            if (!it.flags.castShadow)
                continue;
            ObjectUbo o{};
            o.model = glm::translate(glm::mat4(1.0f), -glm::vec3(cam_.position())) * it.model;
            o.normal = glm::mat4(1.0f);
            o.id = glm::uvec4(0);
            updateUbo(uboObject_, o);
            it.mesh->draw();
            ++stats_.drawCalls;
        }
        shadowInst_.use();
        for (auto& ii : instanced_) {
            if (!ii.flags.castShadow)
                continue;
            ObjectUbo o{};
            o.model = glm::translate(glm::mat4(1.0f), -glm::vec3(cam_.position()));
            o.normal = glm::mat4(1.0f);
            updateUbo(uboObject_, o);
            instBuf_.setData(ii.data, BufferUsage::Stream);
            const_cast<Mesh*>(ii.mesh)->bindInstanceBuffer(instBuf_);
            ii.mesh->drawInstanced(static_cast<int>(ii.data.size()));
            ++stats_.drawCalls;
        }
    }
    glCullFace(GL_BACK);
    stats_.shadowMs = t.ms();
}

void Renderer::bindMaterial(const Material& mat, const TextureSet*& bound) {
    glm::vec3 albedoGain(1.0f);
    float roughnessGain = 1.0f;
    if (texturesOn_ && mat.textured()) {
        // Spec 18 §4 (textures): one bind per run of equal sets — the submit lists are sorted by
        // set, so a batch of n parts sharing a set costs one bind.
        const TextureSet& set = textures_->get(mat.textureSet);
        if (&set != bound) {
            set.bind();
            bound = &set;
            ++stats_.textureBinds;
        }
        albedoGain = 1.0f / set.albedoMean;
        roughnessGain = 1.0f / set.roughnessMean;
    }
    MaterialUbo mu = mat.toUbo(albedoGain, roughnessGain);
    if (!texturesOn_)
        mu.tex.w = 0.0f;
    updateUbo(uboMaterial_, mu);
}

void Renderer::mainPass() {
    core::Timer t;
    msaaFbo_.bind();
    glViewport(0, 0, w_, h_);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    GLfloat cc[4] = {desc_.clearColor.r, desc_.clearColor.g, desc_.clearColor.b, 1.0f};
    glClearBufferfv(GL_COLOR, 0, cc);
    glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    shadowDepth_.bind(5);
    shadowSampler_.bind(5);
    cmaps_.bind(ColormapId::Viridis, 6);
    // IBL (spec 18 §4 amended): irradiance, prefiltered specular, BRDF LUT on units 7–9.
    envIrradiance_.bind(7);
    envPrefiltered_.bind(8);
    brdfLut_.bind(9);
    for (ShaderProgram* p : {&pbr_, &pbrInst_, &pbrTex_, &pbrTexInst_}) {
        p->use();
        p->set("uShadowMap", 5);
        p->set("uColormap", 6);
        p->set("uIrradiance", 7);
        p->set("uPrefiltered", 8);
        p->set("uBrdfLut", 9);
        if (p == &pbrTex_ || p == &pbrTexInst_) {
            p->set("uAlbedoMap", static_cast<int>(kTexUnitAlbedo));
            p->set("uNormalMap", static_cast<int>(kTexUnitNormal));
            p->set("uOrmMap", static_cast<int>(kTexUnitOrm));
        }
    }
    // Batching by texture set (spec 18 §4): untextured items first, then runs of equal sets, so
    // the program switches once and each set binds once per pass.
    auto bySet = [](const auto& a, const auto& b) { return a.mat.textureSet < b.mat.textureSet; };
    std::stable_sort(opaque_.begin(), opaque_.end(), bySet);
    std::stable_sort(instanced_.begin(), instanced_.end(), bySet);
    glm::mat4 camOff = glm::translate(glm::mat4(1.0f), -glm::vec3(cam_.position()));
    const TextureSet* bound = nullptr;
    const ShaderProgram* current = nullptr;
    auto useProgram = [&](const ShaderProgram& p) {
        if (&p != current) {
            p.use();
            current = &p;
        }
    };
    auto drawItem = [&](const Item& it) {
        useProgram(texturesOn_ && it.mat.textured() ? pbrTex_ : pbr_);
        bindMaterial(it.mat, bound);
        ObjectUbo o{};
        o.model = camOff * it.model;
        o.normal = glm::transpose(glm::inverse(it.model));
        o.id = glm::uvec4(it.flags.noPick ? 0u : it.id.value, (it.flags.receiveShadow ? 1u : 0u), 0,
                          0);
        updateUbo(uboObject_, o);
        if (!it.flags.depthTest)
            glDisable(GL_DEPTH_TEST);
        it.mesh->draw();
        ++stats_.drawCalls;
        stats_.triangles += it.mesh->indexCount() / 3;
        if (!it.flags.depthTest)
            glEnable(GL_DEPTH_TEST);
    };
    for (auto& it : opaque_)
        drawItem(it);
    for (auto& ii : instanced_) {
        useProgram(texturesOn_ && ii.mat.textured() ? pbrTexInst_ : pbrInst_);
        bindMaterial(ii.mat, bound);
        ObjectUbo o{};
        o.model = camOff;
        o.normal = glm::mat4(1.0f);
        o.id = glm::uvec4(0, ii.flags.receiveShadow ? 1u : 0u, 0, 0);
        updateUbo(uboObject_, o);
        instBuf_.setData(ii.data, BufferUsage::Stream);
        const_cast<Mesh*>(ii.mesh)->bindInstanceBuffer(instBuf_);
        ii.mesh->drawInstanced(static_cast<int>(ii.data.size()));
        ++stats_.drawCalls;
        stats_.triangles += ii.mesh->indexCount() / 3 * ii.data.size();
    }
    // depth-tested overlay lines
    line_.use();
    current = nullptr;
    lines_.upload();
    lines_.draw();
    stats_.drawCalls += lines_.empty() ? 0 : 1;
    // transparent, sorted back to front, no id write, no depth write
    std::sort(transparent_.begin(), transparent_.end(),
              [](const Item& a, const Item& b) { return a.depth > b.depth; });
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    for (auto& it : transparent_)
        drawItem(it);
    // gizmo lines and text on top
    glDisable(GL_DEPTH_TEST);
    line_.use();
    linesNd_.upload();
    linesNd_.draw();
    if (font_) {
        textSdf_.use();
        textSdf_.set("uAtlas", 0);
        font_->texture().bind(0);
        text_.upload();
        text_.draw();
    }
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    stats_.mainMs = t.ms();
}

void Renderer::idPass() {
    idFbo_.bind();
    glViewport(0, 0, w_, h_);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    GLuint zero[4] = {0, 0, 0, 0};
    glClearBufferuiv(GL_COLOR, 0, zero);
    GLfloat zerof[4] = {0.f, 0.f, 0.f, 0.f}; // linear depth 0 = background for SSAO
    glClearBufferfv(GL_COLOR, 1, zerof);
    glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glm::mat4 camOff = glm::translate(glm::mat4(1.0f), -glm::vec3(cam_.position()));
    idProg_.use();
    for (auto& it : opaque_) {
        // Unpickable geometry writes id 0 (the clear value); it is drawn only when SSAO needs its
        // depth, because `idDepth_` is the SSAO source (spec 18 §5 pass 8).
        const bool unpickable = it.flags.noPick || it.id.value == 0;
        if (unpickable && !desc_.ssao)
            continue;
        ObjectUbo o{};
        o.model = camOff * it.model;
        o.normal = glm::mat4(1.0f);
        o.id = glm::uvec4(unpickable ? 0u : it.id.value, 0, 0, 0);
        updateUbo(uboObject_, o);
        if (!it.flags.depthTest)
            glDisable(GL_DEPTH_TEST);
        it.mesh->draw();
        ++stats_.drawCalls;
        if (!it.flags.depthTest)
            glEnable(GL_DEPTH_TEST);
    }
    idInst_.use();
    for (auto& ii : instanced_) {
        if (ii.flags.noPick && !desc_.ssao)
            continue;
        // bit1 of uObjectId.y tells id.frag to write 0 instead of the per-instance id
        ObjectUbo o{};
        o.model = camOff;
        o.normal = glm::mat4(1.0f);
        o.id = glm::uvec4(0, ii.flags.noPick ? 2u : 0u, 0, 0);
        updateUbo(uboObject_, o);
        instBuf_.setData(ii.data, BufferUsage::Stream);
        const_cast<Mesh*>(ii.mesh)->bindInstanceBuffer(instBuf_);
        ii.mesh->drawInstanced(static_cast<int>(ii.data.size()));
        ++stats_.drawCalls;
    }
}

void Renderer::resolveAndPost() {
    // GPU times of the previous frame's post chain (spec 18 §11): available by now, no stall.
    {
        const double seg[4] = {postQuery_[0].resultMs(), postQuery_[1].resultMs(),
                               postQuery_[2].resultMs(), postQuery_[3].resultMs()};
        stats_.resolveMs = seg[0];
        stats_.aoMs = seg[1];
        stats_.bloomMs = seg[2];
        stats_.postMs = seg[0] + seg[1] + seg[2] + seg[3];
    }
    postQuery_[0].begin();
    msaaFbo_.blitTo(resolveFbo_, 0, 0, w_, h_, GL_LINEAR, false);
    postQuery_[0].end();
    postQuery_[1].begin();
    if (desc_.ssao)
        ssaoPass();
    postQuery_[1].end();
    postQuery_[2].begin();
    if (desc_.bloom)
        bloomPass();
    postQuery_[2].end();
    postQuery_[3].begin();
    // AO multiply + bloom add + outline + tonemap in one post pass
    postFbo_.bind();
    glViewport(0, 0, w_, h_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    postProg_.use();
    resColor_.bind(0);
    resId_.bind(1);
    ssaoTex_.bind(2);
    bloomA_.bind(3);
    postProg_.set("uColor", 0);
    postProg_.set("uId", 1);
    postProg_.set("uAo", 2);
    postProg_.set("uBloom", 3);
    screenQuad_.draw();
    ++stats_.drawCalls;
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    Framebuffer::bindDefault();
    postQuery_[3].end();
}

void Renderer::endFrame() {
    shadowPass();
    mainPass();
    idPass();
    issuePick();
    resolveAndPost();
    VertexArray::unbind(); // leave no mesh VAO bound: a later Mesh::upload must not touch it
    drainGlErrors("Renderer::endFrame");
}

void Renderer::queuePick(int x, int y) {
    pickReq_ = PickRequest{true, x, y};
}

void Renderer::issuePick() {
    PickSlot& slot = pickSlot_[pickWrite_];
    slot.inFlight = false;
    if (pickReq_.queued && pickReq_.x >= 0 && pickReq_.y >= 0 && pickReq_.x < w_ &&
        pickReq_.y < h_) {
        if (!pickPbo_[pickWrite_]) {
            pickPbo_[pickWrite_] = Buffer(GL_PIXEL_PACK_BUFFER);
            std::vector<std::byte> zero(8);
            pickPbo_[pickWrite_].setData(std::span<const std::byte>(zero), BufferUsage::Stream);
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, idFbo_.id());
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        pickPbo_[pickWrite_].bind();
        // With a pack buffer bound the pointer is an offset: id at 0, depth at 4. Neither call
        // waits.
        glReadPixels(pickReq_.x, h_ - 1 - pickReq_.y, 1, 1, GL_RED_INTEGER, GL_UNSIGNED_INT,
                     nullptr);
        glReadPixels(pickReq_.x, h_ - 1 - pickReq_.y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT,
                     reinterpret_cast<void*>(static_cast<std::uintptr_t>(4)));
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        slot = PickSlot{true, pickReq_.x, pickReq_.y, w_, h_, cam_};
    }
    pickReq_.queued = false;
    pickWrite_ ^= 1;
}

std::optional<Renderer::PickResult> Renderer::pollPick() {
    // `pickWrite_` was flipped after the last issue, so it now names the OLDER slot: the one the
    // next endFrame overwrites and the GPU finished a whole frame ago.
    PickSlot& slot = pickSlot_[pickWrite_];
    if (!slot.inFlight)
        return std::nullopt;
    slot.inFlight = false;
    std::uint32_t id = 0;
    float depth = 1.0f;
    pickPbo_[pickWrite_].bind();
    glGetBufferSubData(GL_PIXEL_PACK_BUFFER, 0, 4, &id);
    glGetBufferSubData(GL_PIXEL_PACK_BUFFER, 4, 4, &depth);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    PickResult r;
    r.id = ComponentId{id};
    r.depth = depth;
    r.hit = depth < 1.0f;
    r.x = slot.x;
    r.y = slot.y;
    if (r.hit)
        r.world = slot.cam.unproject(slot.x + 0.5, slot.y + 0.5, depth, slot.w, slot.h);
    return r;
}

void Renderer::blitToScreen(int x, int y, int w, int h) const {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, postFbo_.id());
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBlitFramebuffer(0, 0, w_, h_, x, y, x + w, y + h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

ComponentId Renderer::pick(int x, int y) const {
    if (x < 0 || y < 0 || x >= w_ || y >= h_)
        return ComponentId{0};
    glBindFramebuffer(GL_READ_FRAMEBUFFER, idFbo_.id());
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    std::uint32_t v = 0;
    glReadPixels(x, h_ - 1 - y, 1, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, &v);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    return ComponentId{v};
}

Status Renderer::screenshot(const std::filesystem::path& png) const {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w_) * h_ * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, postFbo_.id());
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    // `glReadPixels` gives row 0 at the BOTTOM, so the write is flipped. The stb flag is global
    // state shared with every other stb writer in the process (`report::writePng`, whose rows
    // already start at the top), so it is restored immediately.
    stbi_flip_vertically_on_write(1);
    const bool ok = stbi_write_png(png.string().c_str(), w_, h_, 4, px.data(), w_ * 4) != 0;
    stbi_flip_vertically_on_write(0);
    if (!ok)
        return fail(ErrorCode::Io, "cannot write " + png.string());
    return {};
}
} // namespace qlab::gfx
