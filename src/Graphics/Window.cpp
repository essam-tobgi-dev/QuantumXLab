#include "Graphics/Window.hpp"
#include "Core/Log.hpp"
#include "Graphics/GlCheck.hpp"
#include "Graphics/GlLoader.hpp"
#include <GLFW/glfw3.h>
#include <mutex>

namespace qlab::gfx {

bool hasCurrentContext() {
    return glfwGetCurrentContext() != nullptr;
}
namespace {
int g_glfwRefs = 0;
std::mutex g_glfwMu;
void glfwErrorCb(int code, const char* msg) {
    QXL_LOG_ERROR(Gfx, "GLFW error {}: {}", code, msg ? msg : "");
}
bool glfwAcquire() {
    std::lock_guard lk(g_glfwMu);
    if (g_glfwRefs == 0) {
        glfwSetErrorCallback(glfwErrorCb);
        if (!glfwInit())
            return false;
    }
    ++g_glfwRefs;
    return true;
}
void glfwRelease() {
    std::lock_guard lk(g_glfwMu);
    if (--g_glfwRefs == 0)
        glfwTerminate();
}
} // namespace

Result<std::unique_ptr<Window>> Window::create(const WindowDesc& desc) {
    if (!glfwAcquire())
        return fail(ErrorCode::Gfx_ + 1, "glfwInit failed");
    auto tryCreate = [&](int major, int minor) {
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, major);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minor);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
        glfwWindowHint(GLFW_VISIBLE, desc.visible ? GLFW_TRUE : GLFW_FALSE);
        glfwWindowHint(GLFW_RESIZABLE, desc.resizable ? GLFW_TRUE : GLFW_FALSE);
        glfwWindowHint(GLFW_SAMPLES, desc.samples);
        glfwWindowHint(GLFW_SRGB_CAPABLE, desc.srgb ? GLFW_TRUE : GLFW_FALSE);
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
#ifdef __APPLE__
        glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);
#endif
        return glfwCreateWindow(desc.width, desc.height, desc.title.c_str(), nullptr, nullptr);
    };
#ifdef __APPLE__
    GLFWwindow* win = tryCreate(4, 1); // macOS caps at 4.1; probing 4.6 only logs a GLFW error
#else
    GLFWwindow* win = tryCreate(4, 6);
#endif
    if (!win)
        win = tryCreate(4, 1);
    if (!win) {
        glfwRelease();
        return fail(ErrorCode::Gfx_ + 2, "could not create an OpenGL 4.1+ core context");
    }
    std::unique_ptr<Window> w(new Window());
    w->win_ = win;
    w->desc_ = desc;
    glfwSetWindowUserPointer(win, w.get());
    glfwMakeContextCurrent(win);
    if (!loadGl(reinterpret_cast<void* (*)(const char*)>(glfwGetProcAddress))) {
        glfwDestroyWindow(win);
        glfwRelease();
        return fail(ErrorCode::Gfx_ + 3, "GL loader failed");
    }
    glfwSwapInterval(desc.vsync ? 1 : 0);
    glfwGetFramebufferSize(win, &w->fbW_, &w->fbH_);
    glfwGetWindowSize(win, &w->winW_, &w->winH_);
    float sx = 1, sy = 1;
    glfwGetWindowContentScale(win, &sx, &sy);
    w->scale_ = sx;
    w->caps_ = Caps::query();
    installCallbacks(w.get());
    if (desc.srgb)
        glEnable(GL_FRAMEBUFFER_SRGB);
    drainGlErrors("Window::create");
    return w;
}

Window::~Window() {
    if (win_) {
        glfwDestroyWindow(win_);
        glfwRelease();
    }
}

void Window::installCallbacks(Window* w) {
    glfwSetFramebufferSizeCallback(w->win_, [](GLFWwindow* g, int fw, int fh) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(g));
        self->fbW_ = fw;
        self->fbH_ = fh;
        glfwGetWindowSize(g, &self->winW_, &self->winH_);
        if (self->onFramebufferResize)
            self->onFramebufferResize(fw, fh);
    });
    glfwSetCursorPosCallback(w->win_, [](GLFWwindow* g, double x, double y) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(g));
        double s = self->scale_;
        double nx = x * s, ny = y * s;
        self->input_.mouseDx += nx - self->input_.mouseX;
        self->input_.mouseDy += ny - self->input_.mouseY;
        self->input_.mouseX = nx;
        self->input_.mouseY = ny;
    });
    glfwSetMouseButtonCallback(w->win_, [](GLFWwindow* g, int b, int action, int mods) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(g));
        if (b >= 0 && b < 8)
            self->input_.mouseDown[static_cast<std::size_t>(b)] = (action != GLFW_RELEASE);
        self->input_.shift = mods & GLFW_MOD_SHIFT;
        self->input_.ctrl = mods & GLFW_MOD_CONTROL;
        self->input_.alt = mods & GLFW_MOD_ALT;
        self->input_.super = mods & GLFW_MOD_SUPER;
    });
    glfwSetScrollCallback(w->win_, [](GLFWwindow* g, double x, double y) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(g));
        self->input_.scrollX += x;
        self->input_.scrollY += y;
    });
    glfwSetKeyCallback(w->win_, [](GLFWwindow* g, int key, int, int action, int mods) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(g));
        if (key >= 0 && key < 512)
            self->input_.keyDown[static_cast<std::size_t>(key)] = (action != GLFW_RELEASE);
        self->input_.shift = mods & GLFW_MOD_SHIFT;
        self->input_.ctrl = mods & GLFW_MOD_CONTROL;
        self->input_.alt = mods & GLFW_MOD_ALT;
        self->input_.super = mods & GLFW_MOD_SUPER;
    });
    glfwSetCharCallback(w->win_, [](GLFWwindow* g, unsigned int c) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(g));
        self->input_.chars.push_back(c);
    });
    glfwSetDropCallback(w->win_, [](GLFWwindow* g, int count, const char** paths) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(g));
        if (!self->onDrop)
            return;
        std::vector<std::string> v;
        for (int i = 0; i < count; ++i)
            v.emplace_back(paths[i]);
        self->onDrop(v);
    });
}

void Window::pollEvents() {
    input_.beginPoll();
    glfwPollEvents();
}
void Window::swapBuffers() {
    glfwSwapBuffers(win_);
}
bool Window::shouldClose() const {
    return glfwWindowShouldClose(win_);
}
void Window::requestClose() {
    glfwSetWindowShouldClose(win_, GLFW_TRUE);
}
void Window::makeCurrent() {
    glfwMakeContextCurrent(win_);
}
void Window::setVsync(bool on) {
    glfwSwapInterval(on ? 1 : 0);
}
void Window::setTitle(const std::string& t) {
    glfwSetWindowTitle(win_, t.c_str());
}
double Window::time() const {
    return glfwGetTime();
}
} // namespace qlab::gfx
