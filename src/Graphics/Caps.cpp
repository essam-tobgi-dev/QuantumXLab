#include "Graphics/Caps.hpp"
#include "Graphics/GlLoader.hpp"
#include "Core/Log.hpp"
#include <cstring>
namespace qlab::gfx {
Caps Caps::query() {
    Caps c;
    glGetIntegerv(GL_MAJOR_VERSION, &c.major);
    glGetIntegerv(GL_MINOR_VERSION, &c.minor);
    c.dsa = c.atLeast(4, 5);
    c.computeShaders = c.atLeast(4, 3);
    c.bufferStorage = c.atLeast(4, 4);
    c.textureStorage = c.atLeast(4, 2);
    c.debugOutput = c.atLeast(4, 3);
    glGetIntegerv(GL_MAX_SAMPLES, &c.maxSamples);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &c.maxTextureSize);
    GLint nExt = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &nExt);
    for (GLint i = 0; i < nExt; ++i) {
        const char* e = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (!e) continue;
        if (std::strcmp(e, "GL_EXT_texture_filter_anisotropic") == 0 || std::strcmp(e, "GL_ARB_texture_filter_anisotropic") == 0) c.anisotropic = true;
        if (std::strcmp(e, "GL_KHR_debug") == 0) c.debugOutput = true;
    }
    if (c.anisotropic) {
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &c.maxAnisotropy);
    }
    auto str = [](GLenum e) { const GLubyte* s = glGetString(e); return s ? std::string(reinterpret_cast<const char*>(s)) : std::string(); };
    c.renderer = str(GL_RENDERER); c.vendor = str(GL_VENDOR); c.version = str(GL_VERSION); c.glsl = str(GL_SHADING_LANGUAGE_VERSION);
    QXL_LOG_INFO(Gfx, "OpenGL {}.{} {} | {} | GLSL {} | samples {} | compute {}", c.major, c.minor, c.renderer, c.vendor, c.glsl, c.maxSamples, c.computeShaders);
    return c;
}
} // namespace qlab::gfx
