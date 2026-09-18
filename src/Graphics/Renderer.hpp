#pragma once
// Spec 18 §4/§7 — renderer façade. Callers submit meshes/instances/lines/text each frame;
// endFrame runs: shadow → PBR (MSAA RGBA16F) → transparent → resolve → id pass (R32UI, single-sample) → post (outline+tonemap).
#include "Core/Error.hpp"
#include "Core/StrongType.hpp"
#include "Graphics/Camera.hpp"
#include "Graphics/Colormap.hpp"
#include "Graphics/LineBatch.hpp"
#include "Graphics/Material.hpp"
#include "Graphics/Mesh.hpp"
#include "Graphics/Shader.hpp"
#include "Graphics/TextBatch.hpp"
#include "Graphics/TextureLibrary.hpp"
#include "Graphics/Ubo.hpp"
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace qlab::gfx {

struct InstanceData {         // must match Mesh::bindInstanceBuffer layout (96 bytes)
    glm::mat4 model;
    glm::uvec2 idFlags;
    glm::vec2 pad_;
    glm::vec4 color;
};
static_assert(sizeof(InstanceData) == 96);

struct SubmitFlags { bool castShadow = true; bool receiveShadow = true; bool depthTest = true; bool noPick = false; };

struct LodGroup {
    std::vector<const Mesh*> meshes;   // finest first
    std::vector<float> distances;      // switch distance for meshes[i] -> meshes[i+1]
    const Mesh* select(float dist) const;
};

// shadowMs/mainMs are CPU submit times. postMs (resolve → SSAO → bloom → outline/tonemap), aoMs
// and bloomMs are GPU times from timer queries and describe the PREVIOUS frame (spec 18 §11:
// results are read a frame late so no query ever stalls the pipeline).
struct FrameStats { int drawCalls = 0; int textureBinds = 0; std::size_t triangles = 0; double shadowMs = 0, mainMs = 0, postMs = 0, aoMs = 0, bloomMs = 0, resolveMs = 0; };

struct RendererDesc {
    int samples = 4;
    int shadowSize = 4096;
    glm::vec3 clearColor{0.07f, 0.08f, 0.10f};
    float exposure = 1.0f;
    std::filesystem::path fontTtf;     // default: Assets/Fonts/Inter-Regular.ttf
    // Spec 18 §4/§5 (amended): image-based lighting from the procedural laboratory environment,
    // screen-space ambient occlusion and bloom. Each can be disabled independently.
    bool ibl = true;
    bool ssao = true;
    bool bloom = true;
    float aoStrength = 0.8f;           // post: color *= mix(1, ao, aoStrength)
    float bloomStrength = 0.12f;       // post: color += bloom * bloomStrength
    std::filesystem::path textureRoot; // texture sets (spec 18 §4); default: Assets/Textures
};

class Renderer {
public:
    static Result<std::unique_ptr<Renderer>> create(const RendererDesc& d);
    ~Renderer();

    void beginFrame(int viewportW, int viewportH, const Camera& cam, double timeS);
    void submit(const Mesh& m, const Material& mat, const glm::mat4& model, ComponentId id = ComponentId{0}, SubmitFlags f = {});
    void submitInstanced(const Mesh& m, const Material& mat, std::span<const InstanceData> inst, SubmitFlags f = {});
    LineBatch& lines() { return lines_; }             // WORLD-space overlay lines (depth tested); call after beginFrame
    LineBatch& linesNoDepth() { return linesNd_; }    // gizmos drawn on top
    TextBatch& text() { return text_; }
    void setSelection(ComponentId selected, ComponentId hovered);
    void setSun(glm::vec3 dir, glm::vec3 color, float intensity);
    void setAmbient(glm::vec3 c) { lights_.ambient = glm::vec4(c, lights_.ambient.w); }
    void setPointLights(std::span<const PointLight> pts);
    void setExposure(float e) { desc_.exposure = e; }
    void setEnvironmentIntensity(float k) { envIntensity_ = k; }   // multiplier on the IBL (default 1)
    float exposure() const { return desc_.exposure; }
    float environmentIntensity() const { return envIntensity_; }
    void endFrame();                                   // renders into the internal target
    // Presents the final image to the currently bound framebuffer at (x,y,w,h).
    void blitToScreen(int x, int y, int w, int h) const;
    const Texture2D& colorTexture() const { return postTex_; }
    const Texture2D& hdrTexture() const { return resColor_; }   // resolved linear RGBA16F, before post
    // Texture sets of the TEXTURED permutation (spec 18 §4): loaded on first use by set name.
    TextureLibrary& textures() { return *textures_; }
    void setTexturesEnabled(bool on) { texturesOn_ = on; }   // off: every material draws untextured
    bool texturesEnabled() const { return texturesOn_; }

    ComponentId pick(int x, int y) const;              // pixel in viewport coords, y down; SYNCHRONOUS (stalls)

    // Spec 18 §5 pass 11 — asynchronous picking for the interactive viewport. `queuePick` asks for
    // the id and depth under a pixel; `endFrame` copies them into a pixel-pack buffer after the id
    // pass; `pollPick` returns the pick that `endFrame` issued the frame BEFORE last, which the GPU
    // has long finished, so neither call waits on the GPU. One pick per frame; latency two frames.
    // `world` is the surface point under the pixel, unprojected with the camera of that frame.
    struct PickResult {
        ComponentId id{0};
        bool hit = false;          // false when the pixel shows the background
        glm::dvec3 world{0.0};
        float depth = 1.0f;        // GL window depth
        int x = 0, y = 0;
    };
    void queuePick(int x, int y);
    std::optional<PickResult> pollPick();
    Status screenshot(const std::filesystem::path& png) const;
    const FrameStats& stats() const { return stats_; }
    const SdfFont* font() const { return font_.get(); }
    ColormapTextures& colormaps() { return cmaps_; }
    const Camera& camera() const { return cam_; }
    // IBL resources (read-only; tests read them back). Generated once in init (spec 18 §5 EnvPrefilter).
    const TextureCube& environment() const { return envSrc_; }
    const TextureCube& prefilteredEnvironment() const { return envPrefiltered_; }
    const TextureCube& irradianceMap() const { return envIrradiance_; }
    const Texture2D& brdfLut() const { return brdfLut_; }
    const Texture2D& aoTexture() const { return ssaoTex_; }
    const RendererDesc& desc() const { return desc_; }

private:
    Renderer() = default;
    Status init(const RendererDesc& d);
    void resize(int w, int h);
    void shadowPass();
    void mainPass();
    void idPass();   // GL_MAX_INTEGER_SAMPLES is 1 on macOS: ids are drawn in a separate single-sample pass
    void resolveAndPost();
    void ssaoPass();     // spec 18 §5 pass 8: depth of the id pass → R8 occlusion, bilateral blur
    void bloomPass();    // bright pass at half resolution, 2 × separable 9-tap Gaussian
    Status buildEnvironment();   // RendererEnv.cpp: env cubemap, prefiltered mips, irradiance, BRDF LUT
    void drawCubeFaces(const ShaderProgram& prog, TextureCube& target, int level, Framebuffer& fbo);
    void issuePick();   // after idPass: the queued pick → the pixel-pack buffer of this frame's slot
    // Binds the item's texture set once per run of equal sets and fills the material UBO.
    void bindMaterial(const Material& mat, const TextureSet*& bound);
    struct Item { const Mesh* mesh; Material mat; glm::mat4 model; ComponentId id; SubmitFlags flags; float depth; };
    struct InstItem { const Mesh* mesh; Material mat; std::vector<InstanceData> data; SubmitFlags flags; };
    std::vector<Item> opaque_, transparent_;
    std::vector<InstItem> instanced_;
    LineBatch lines_, linesNd_;
    TextBatch text_;
    std::unique_ptr<SdfFont> font_;
    std::unique_ptr<TextureLibrary> textures_;
    ColormapTextures cmaps_;
    RendererDesc desc_;
    Camera cam_;
    int w_ = 0, h_ = 0;
    double time_ = 0;
    // GL resources
    ShaderProgram pbr_, pbrInst_, pbrTex_, pbrTexInst_, shadow_, shadowInst_, idProg_, idInst_, line_, textSdf_, postProg_, outline_;
    ShaderProgram envLab_, envPrefilter_, envIrrProg_, brdfLutProg_, ssaoProg_, ssaoBlur_, ssaoUpsample_, bloomBright_, bloomBlur_;
    TextureCube envSrc_, envPrefiltered_, envIrradiance_;
    Texture2D brdfLut_;
    Framebuffer envFbo_, ssaoRawFbo_, ssaoHFbo_, ssaoFbo_, bloomFboA_, bloomFboB_;
    Texture2D idLinDepth_;               // R32F linear view depth, attachment 1 of the id pass
    Texture2D ssaoRaw_, ssaoH_, ssaoTex_;  // half-res RG16F (ao, depth) ×2, full-res R8 result
    Texture2D bloomA_, bloomB_;          // half-res R11G11B10F ping-pong
    TimerQuery postQuery_[4];      // consecutive GL_TIME_ELAPSED segments: resolve, SSAO, bloom, composite
    float envIntensity_ = 1.0f;
    bool texturesOn_ = true;
    Buffer uboFrame_, uboLights_, uboMaterial_, uboObject_, uboSelection_;
    Framebuffer msaaFbo_, resolveFbo_, idFbo_, postFbo_, shadowFbo_;
    Texture2D msaaColor_, msaaDepth_, resColor_, resId_, idDepth_, postTex_, shadowDepth_;
    Buffer instBuf_;
    Mesh screenQuad_;
    Sampler shadowSampler_;
    LightsUbo lights_{};
    SelectionUbo sel_{};
    FrameStats stats_;
    std::vector<std::uint32_t> idReadback_;
    // async pick: one request, two pixel-pack slots (see queuePick)
    struct PickRequest { bool queued = false; int x = 0, y = 0; } pickReq_;
    struct PickSlot { bool inFlight = false; int x = 0, y = 0, w = 0, h = 0; Camera cam; };
    PickSlot pickSlot_[2];
    Buffer pickPbo_[2];
    int pickWrite_ = 0;   // the slot the next endFrame writes; the other one holds last frame's pick
};

} // namespace qlab::gfx
