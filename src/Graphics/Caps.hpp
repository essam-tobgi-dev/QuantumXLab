#pragma once
// Spec 18 §1 — capability struct filled once from the live context. Code branches on Caps only.
#include <string>
namespace qlab::gfx {
struct Caps {
    int major = 0, minor = 0;
    bool dsa = false;            // 4.5+
    bool computeShaders = false; // 4.3+ (never on macOS)
    bool debugOutput = false;    // 4.3+ / KHR_debug
    bool bufferStorage = false;  // 4.4+
    bool textureStorage = false; // 4.2+
    bool anisotropic = false;
    float maxAnisotropy = 1.0f;
    int maxSamples = 1;
    int maxTextureSize = 0;
    std::string renderer, vendor, version, glsl;
    static Caps query();
    bool atLeast(int mj, int mn) const { return major > mj || (major == mj && minor >= mn); }
};
} // namespace qlab::gfx
