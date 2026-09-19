#pragma once
// Spec 18 §1 — GLFW window + GL context + input state. Headless mode uses a hidden window.
#include "Core/Error.hpp"
#include "Graphics/Caps.hpp"
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

namespace qlab::gfx {

struct InputState {
    double mouseX = 0, mouseY = 0;   // framebuffer pixels
    double mouseDx = 0, mouseDy = 0; // since last poll
    double scrollX = 0, scrollY = 0; // accumulated since last poll
    std::array<bool, 8> mouseDown{};
    std::array<bool, 512> keyDown{};
    bool shift = false, ctrl = false, alt = false, super = false;
    std::vector<unsigned int> chars; // text input since last poll
    void beginPoll() {
        mouseDx = mouseDy = 0;
        scrollX = scrollY = 0;
        chars.clear();
    }
};

struct WindowDesc {
    int width = 1600, height = 1000;
    std::string title = "QuantumXLab";
    bool visible = true; // false = headless / hidden (tests, --selftest)
    bool vsync = true;
    bool resizable = true;
    int samples = 0; // default framebuffer MSAA (viewport has its own FBO); keep 0
    bool srgb = true;
};

class Window {
  public:
    static Result<std::unique_ptr<Window>> create(const WindowDesc& desc);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    void pollEvents();
    void swapBuffers();
    bool shouldClose() const;
    void requestClose();
    void makeCurrent();
    void setVsync(bool on);
    void setTitle(const std::string& t);

    int framebufferWidth() const { return fbW_; }
    int framebufferHeight() const { return fbH_; }
    int windowWidth() const { return winW_; }
    int windowHeight() const { return winH_; }
    float contentScale() const { return scale_; }
    double time() const;
    bool headless() const { return !desc_.visible; }

    const InputState& input() const { return input_; }
    InputState& input() { return input_; }
    const Caps& caps() const { return caps_; }
    GLFWwindow* handle() const { return win_; }

    std::function<void(int, int)> onFramebufferResize;
    std::function<void(const std::vector<std::string>&)> onDrop;

  private:
    Window() = default;
    static void installCallbacks(Window* w);
    GLFWwindow* win_ = nullptr;
    WindowDesc desc_;
    Caps caps_;
    InputState input_;
    int fbW_ = 0, fbH_ = 0, winW_ = 0, winH_ = 0;
    float scale_ = 1.0f;
};

} // namespace qlab::gfx
