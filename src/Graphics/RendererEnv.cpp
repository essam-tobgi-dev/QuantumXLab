// Spec 18 §4/§5 (amended) — image-based lighting resources, generated once at init on the GPU:
//   env_lab        → envSrc_ 256² RGB16F cubemap with a full mip chain (source for the filters)
//   env_prefilter  → envPrefiltered_ 6 levels 256…8, level k ↔ roughness k/5 (GGX, split sum)
//   env_irradiance → envIrradiance_ 32² cosine-lobe convolution (E/π), GPU, from source mip 3
//   brdf_lut       → brdfLut_ 128² RG16F (scale A, bias B over n·v × roughness)
#include "Core/Log.hpp"
#include "Core/Timer.hpp"
#include "Graphics/GlCheck.hpp"
#include "Graphics/Renderer.hpp"
#include <cmath>

namespace qlab::gfx {

void Renderer::drawCubeFaces(const ShaderProgram& prog, TextureCube& target, int level,
                             Framebuffer& fbo) {
    const int sz = target.levelSize(level);
    glViewport(0, 0, sz, sz);
    for (int face = 0; face < 6; ++face) {
        fbo.attachCubeFace(0, target, face, level);
        fbo.setDrawBuffers(1);
        prog.set("uFace", face);
        screenQuad_.draw();
    }
}

Status Renderer::buildEnvironment() {
    core::Timer t;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    envFbo_ = Framebuffer();

    // 1. Source environment: pure function of direction (env_lab.frag).
    CubeDesc src;
    src.size = 256;
    src.format = TexFormat::RGB16F;
    src.levels = 9; // 256 → 1
    envSrc_ = TextureCube(src);
    envFbo_.attachCubeFace(0, envSrc_, 0, 0);
    envFbo_.setDrawBuffers(1);
    if (auto st = envFbo_.check(); !st) {
        // RGB16F is not required to be colour-renderable on GL 4.1; fall back to RGBA16F.
        QXL_LOG_WARN(Gfx, "RGB16F cubemap not renderable ({}); using RGBA16F", st.error().message);
        src.format = TexFormat::RGBA16F;
        envSrc_ = TextureCube(src);
        envFbo_.attachCubeFace(0, envSrc_, 0, 0);
        envFbo_.setDrawBuffers(1);
        QXL_TRY(envFbo_.check());
    }
    envLab_.use();
    drawCubeFaces(envLab_, envSrc_, 0, envFbo_);
    envSrc_.generateMipmaps();

    // 2. Prefiltered specular: level k ↔ roughness k/5, sampling the source mips by pdf.
    CubeDesc pre = src;
    pre.levels = 6; // 256, 128, 64, 32, 16, 8
    envPrefiltered_ = TextureCube(pre);
    envPrefilter_.use();
    envSrc_.bind(0);
    envPrefilter_.set("uEnv", 0);
    envPrefilter_.set("uSrcSize", static_cast<float>(src.size));
    static constexpr unsigned kSamples[6] = {1u, 128u, 512u, 2048u, 2048u, 2048u}; // per level
    for (int level = 0; level < pre.levels; ++level) {
        envPrefilter_.set("uRoughness",
                          static_cast<float>(level) / static_cast<float>(pre.levels - 1));
        envPrefilter_.setUnsigned("uSamples", kSamples[level]);
        drawCubeFaces(envPrefilter_, envPrefiltered_, level, envFbo_);
    }

    // 3. Irradiance: 32² cosine convolution of source mip 3 (32² faces).
    CubeDesc irr = src;
    irr.size = 32;
    irr.levels = 1;
    envIrradiance_ = TextureCube(irr);
    envIrrProg_.use();
    envSrc_.bind(0);
    envIrrProg_.set("uEnv", 0);
    envIrrProg_.set("uSrcMip", 3.0f);
    drawCubeFaces(envIrrProg_, envIrradiance_, 0, envFbo_);

    // 4. BRDF LUT 128² RG16F.
    TexDesc ld;
    ld.width = ld.height = 128;
    ld.format = TexFormat::RG16F;
    ld.linear = true;
    ld.clampToEdge = true;
    brdfLut_ = Texture2D(ld);
    Framebuffer lutFbo;
    lutFbo.attachColor(0, brdfLut_);
    lutFbo.setDrawBuffers(1);
    QXL_TRY(lutFbo.check());
    glViewport(0, 0, 128, 128);
    brdfLutProg_.use();
    screenQuad_.draw();

    Framebuffer::bindDefault();
    VertexArray::unbind();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    drainGlErrors("Renderer::buildEnvironment");
    glFinish(); // one-time init: wait so the logged cost is the real GPU time (budget 150 ms)
    QXL_LOG_INFO(Gfx, "environment prefiltered in {:.1f} ms", t.ms());
    return {};
}

} // namespace qlab::gfx
