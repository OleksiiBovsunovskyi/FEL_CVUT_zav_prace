module;
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include <cctype>
#include <string>

module VkWindow;

import Logger;

AppWindow* AppWindow::instance_ = nullptr;

namespace {

GLFWwindow* asWindow(void* p) { return static_cast<GLFWwindow*>(p); }

// --- GLFW trampolines: forward the C callbacks into the AppWindow instance ---

void glfwErrorCB(int code, const char* description) {
    logError("GLFW error " + std::to_string(code) + ": " +
             (description ? description : "unknown"));
}

void glfwFramebufferSizeCB(GLFWwindow*, int width, int height) {
    if (auto* w = AppWindow::current()) w->onFramebufferSize(width, height);
}

void glfwKeyCB(GLFWwindow*, int key, int /*scancode*/, int action, int /*mods*/) {
    if (auto* w = AppWindow::current()) w->onKey(key, action);
}

void glfwCursorPosCB(GLFWwindow*, double x, double y) {
    if (auto* w = AppWindow::current()) w->onCursorPos(x, y);
}

void glfwMouseButtonCB(GLFWwindow*, int button, int action, int /*mods*/) {
    if (auto* w = AppWindow::current()) w->onMouseButton(button, action);
}

} // namespace


bool AppWindow::init(int width, int height, const char* title) {
    instance_ = this;
    width_   = width;
    height_  = height;
    centerX_ = width  / 2;
    centerY_ = height / 2;

    glfwSetErrorCallback(glfwErrorCB);

    if (!glfwInit()) {
        logError("AppWindow: glfwInit failed");
        return false;
    }

    if (!glfwVulkanSupported()) {
        logError("AppWindow: no Vulkan loader/ICD found by GLFW");
        return false;
    }

    // No client API: the swapchain owns the images, GLFW must not create a context.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    handle_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!handle_) {
        logError("AppWindow: failed to create GLFW window");
        glfwTerminate();
        return false;
    }

    // Installed before ImGui's platform backend so that its own callbacks chain
    // into these instead of replacing them.
    glfwSetFramebufferSizeCallback(asWindow(handle_), glfwFramebufferSizeCB);
    glfwSetKeyCallback(asWindow(handle_),             glfwKeyCB);
    glfwSetCursorPosCallback(asWindow(handle_),       glfwCursorPosCB);
    glfwSetMouseButtonCallback(asWindow(handle_),     glfwMouseButtonCB);

    if (!initImGui())
        return false;

    applyCursorMode();
    return true;
}

bool AppWindow::initImGui() {
    IMGUI_CHECKVERSION();
    if (!ImGui::CreateContext()) {
        logError("AppWindow: ImGui::CreateContext failed");
        return false;
    }
    ImGui::StyleColorsDark();

    // Platform backend only; the renderer backend (ImGui_ImplVulkan_Init) needs
    // a device and a descriptor pool and is set up by the app.
    if (!ImGui_ImplGlfw_InitForVulkan(asWindow(handle_), true)) {
        logError("AppWindow: ImGui_ImplGlfw_InitForVulkan failed");
        return false;
    }
    return true;
}

void AppWindow::mainLoop() {
    while (!glfwWindowShouldClose(asWindow(handle_))) {
        glfwPollEvents();

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (onUI_) onUI_();

        // The draw callback records the command buffer, so the draw data has to
        // exist before it runs - unlike the GL path, where the UI is rendered
        // after the scene.
        ImGui::Render();

        if (onDraw_) onDraw_();
    }
}

void AppWindow::shutdown() {
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (handle_) {
        glfwDestroyWindow(asWindow(handle_));
        handle_ = nullptr;
    }
    glfwTerminate();
    instance_ = nullptr;
}

bool AppWindow::createSurface(VkInstance instance, VkSurfaceKHR* outSurface) const {
    const VkResult res = glfwCreateWindowSurface(instance, asWindow(handle_), nullptr, outSurface);
    if (res != VK_SUCCESS) {
        logError("AppWindow: glfwCreateWindowSurface failed: VkResult " + std::to_string(res));
        return false;
    }
    return true;
}

void AppWindow::getFramebufferSize(int& width, int& height) const {
    width = height = 0;
    if (handle_) glfwGetFramebufferSize(asWindow(handle_), &width, &height);
}

void AppWindow::waitWhileMinimized() const {
    int w = 0, h = 0;
    getFramebufferSize(w, h);
    while (w == 0 || h == 0) {
        glfwWaitEvents();
        getFramebufferSize(w, h);
    }
}

int AppWindow::getElapsedMs() const {
    return static_cast<int>(glfwGetTime() * 1000.0);
}

bool AppWindow::isKeyDown(unsigned char key) const {
    // GLFW reports letters as uppercase ASCII; accept either case.
    return keys_[static_cast<unsigned char>(std::toupper(key))];
}

void AppWindow::setUIMode(bool enable) {
    uiMode_ = enable;
    applyCursorMode();
}

void AppWindow::applyCursorMode() {
    if (!handle_) return;

    if (uiMode_) {
        glfwSetInputMode(asWindow(handle_), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    } else {
        // GLFW_CURSOR_DISABLED gives unbounded relative motion, which replaces
        // the old warp-pointer-to-centre trick entirely.
        glfwSetInputMode(asWindow(handle_), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported())
            glfwSetInputMode(asWindow(handle_), GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        firstMouse_ = true;   // drop the jump caused by re-centring
    }
}

void AppWindow::onFramebufferSize(int w, int h) {
    if (w <= 0 || h <= 0) return;   // minimised
    width_   = w;
    height_  = h;
    centerX_ = w / 2;
    centerY_ = h / 2;
    if (onResize_) onResize_(w, h);
}

void AppWindow::onKey(int key, int action) {
    if (action == GLFW_REPEAT) return;
    const bool down = (action == GLFW_PRESS);

    if (down && key == GLFW_KEY_ESCAPE) {
        glfwSetWindowShouldClose(asWindow(handle_), GLFW_TRUE);
        return;
    }
    if (down && key == GLFW_KEY_TAB) {
        setUIMode(!uiMode_);
        return;
    }

    // Let ImGui have the keyboard while a widget is focused.
    if (ImGui::GetIO().WantCaptureKeyboard) return;

    if (key < 0 || key > 255) return;
    const auto ascii = static_cast<unsigned char>(key);

    keys_[ascii] = down;
    if (down && onKeyDown_) onKeyDown_(ascii);
}

void AppWindow::onCursorPos(double x, double y) {
    if (uiMode_) return;
    if (ImGui::GetIO().WantCaptureMouse) return;

    if (firstMouse_) {
        lastMouseX_ = x;
        lastMouseY_ = y;
        firstMouse_ = false;
        return;
    }

    const double dx = x - lastMouseX_;
    const double dy = y - lastMouseY_;
    lastMouseX_ = x;
    lastMouseY_ = y;

    if (onMouseMove_)
        onMouseMove_(static_cast<float>(dx), static_cast<float>(dy));
}

void AppWindow::onMouseButton(int button, int action) {
    if (uiMode_ || ImGui::GetIO().WantCaptureMouse) return;
    if (onMouseButton_) onMouseButton_(button, action);
}
