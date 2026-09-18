#pragma once
// Spec 18 §1 / 01 §2 — OpenGL entry points. macOS: native <OpenGL/gl3.h> (4.1 core).
// Other platforms: GLAD 2 loader hook (generated header expected at third_party/glad/gl.h).
#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#define QXL_GL_NATIVE_MACOS 1
#else
// ---- GLAD hook (Tier 2, Windows/Linux). Generate GLAD 2 for GL 4.6 core + GL_KHR_debug and
// place it at third_party/glad/gl.h; qlab::gfx::loadGl() then calls gladLoadGL(glfwGetProcAddress).
#include <glad/gl.h>
#define QXL_GL_GLAD 1
#endif

namespace qlab::gfx {
// Loads function pointers where a loader is required; returns false on failure. No-op on macOS.
bool loadGl(void* (*getProcAddress)(const char*));
} // namespace qlab::gfx
