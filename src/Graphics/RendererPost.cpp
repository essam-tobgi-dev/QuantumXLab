// Spec 18 §5 (amended) — post-chain passes on the resolved frame:
//   pass 8  Ssao  : linear depth of the id pass → half-res (ao, depth) → bilateral H (half-res)
//                   → bilateral V + depth-aware upsample → R8 full-res (ssaoTex_)
//   pass 8b Bloom : bright pass fused into the first blur; 2 × (H+V) 9-tap Gaussians as 5
//                   bilinear fetches each, at half resolution (result in bloomA_)
#include "Graphics/GlCheck.hpp"
#include "Graphics/Renderer.hpp"

namespace qlab::gfx {

void Renderer::ssaoPass() {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    const int hw = ssaoRaw_.width(), hh = ssaoRaw_.height();
    const glm::vec2 halfTexel(1.0f / static_cast<float>(hw), 1.0f / static_cast<float>(hh));
    // raw occlusion at half resolution
    ssaoRawFbo_.bind();
    glViewport(0, 0, hw, hh);
    ssaoProg_.use();
    idLinDepth_.bind(0);
    ssaoProg_.set("uDepth", 0);
    screenQuad_.draw();
    ++stats_.drawCalls;
    // horizontal bilateral blur, half resolution
    ssaoHFbo_.bind();
    ssaoBlur_.use();
    ssaoRaw_.bind(1);
    ssaoBlur_.set("uAo", 1);
    ssaoBlur_.set("uDir", glm::vec2(halfTexel.x, 0.0f));
    screenQuad_.draw();
    ++stats_.drawCalls;
    // vertical bilateral blur + depth-aware upsample to the full-resolution R8 result
    ssaoFbo_.bind();
    glViewport(0, 0, w_, h_);
    ssaoUpsample_.use();
    ssaoH_.bind(1);
    ssaoUpsample_.set("uAo", 1);
    ssaoUpsample_.set("uDepth", 0);
    ssaoUpsample_.set("uDir", glm::vec2(0.0f, halfTexel.y));
    screenQuad_.draw();
    ++stats_.drawCalls;
}

void Renderer::bloomPass() {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    const int bw = bloomA_.width(), bh = bloomA_.height();
    glViewport(0, 0, bw, bh);
    const glm::vec2 dx(1.0f / static_cast<float>(bw), 0.0f),
        dy(0.0f, 1.0f / static_cast<float>(bh));
    // bright pass + horizontal blur: resolved HDR (full res, bilinear) → B
    bloomFboB_.bind();
    bloomBright_.use();
    resColor_.bind(0);
    bloomBright_.set("uColor", 0);
    bloomBright_.set("uDir", dx);
    screenQuad_.draw();
    ++stats_.drawCalls;
    bloomBlur_.use();
    bloomBlur_.set("uColor", 0);
    // V: B → A, H: A → B, V: B → A  (two full iterations; the result lives in A)
    bloomFboA_.bind();
    bloomB_.bind(0);
    bloomBlur_.set("uDir", dy);
    screenQuad_.draw();
    bloomFboB_.bind();
    bloomA_.bind(0);
    bloomBlur_.set("uDir", dx);
    screenQuad_.draw();
    bloomFboA_.bind();
    bloomB_.bind(0);
    bloomBlur_.set("uDir", dy);
    screenQuad_.draw();
    stats_.drawCalls += 3;
}

} // namespace qlab::gfx
