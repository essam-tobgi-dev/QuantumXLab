#include "Graphics/GlCheck.hpp"
#include "Graphics/GlLoader.hpp"
#include "Core/Log.hpp"
namespace qlab::gfx {
bool loadGl(void* (*getProcAddress)(const char*)) {
#if defined(QXL_GL_GLAD)
    return gladLoadGL((GLADloadfunc)getProcAddress) != 0;
#else
    (void)getProcAddress;
    return true;
#endif
}
int drainGlErrors(std::string_view where) {
    int n = 0;
    for (GLenum e = glGetError(); e != GL_NO_ERROR; e = glGetError()) {
        const char* name = "GL_ERROR";
        switch (e) {
        case GL_INVALID_ENUM: name = "GL_INVALID_ENUM"; break;
        case GL_INVALID_VALUE: name = "GL_INVALID_VALUE"; break;
        case GL_INVALID_OPERATION: name = "GL_INVALID_OPERATION"; break;
        case GL_INVALID_FRAMEBUFFER_OPERATION: name = "GL_INVALID_FRAMEBUFFER_OPERATION"; break;
        case GL_OUT_OF_MEMORY: name = "GL_OUT_OF_MEMORY"; break;
        default: break;
        }
        QXL_LOG_ERROR(Gfx, "{} at {}", name, where);
        ++n;
        if (n > 16) break;
    }
    return n;
}
} // namespace qlab::gfx
