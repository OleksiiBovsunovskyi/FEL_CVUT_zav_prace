module;
#include "pgr.h"

// pgr.h already pulled in the glloadgen GL header; GLFW_INCLUDE_NONE stops GLFW
// from dragging in a second, conflicting set of GL declarations.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <cctype>

module Window;

AppWindow* AppWindow::instance_ = nullptr;

namespace {

/// GLSL version string handed to the ImGui GL3 backend; must match the context.
constexpr const char* IMGUI_GLSL_VERSION = "#version 460";

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


bool AppWindow::init(int /*argc*/, char** /*argv*/, int width, int height, const char* title) {
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

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, pgr::OGL_VER_MAJOR);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, pgr::OGL_VER_MINOR);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifndef NDEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

    handle_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!handle_) {
        logError("AppWindow: failed to create GLFW window");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(asWindow(handle_));
    glfwSwapInterval(0);   // uncapped: frame timings must not be vsync-limited

    glfwSetFramebufferSizeCallback(asWindow(handle_), glfwFramebufferSizeCB);
    glfwSetKeyCallback(asWindow(handle_),             glfwKeyCB);
    glfwSetCursorPosCallback(asWindow(handle_),       glfwCursorPosCB);
    glfwSetMouseButtonCallback(asWindow(handle_),     glfwMouseButtonCB);

    // pgr::initialize loads the GL entry points, so it needs a current context.
    if (!pgr::initialize(pgr::OGL_VER_MAJOR, pgr::OGL_VER_MINOR)) {
        logError("AppWindow: pgr::initialize failed");
        return false;
    }

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

    if (!ImGui_ImplGlfw_InitForOpenGL(asWindow(handle_), true)) {
        logError("AppWindow: ImGui_ImplGlfw_InitForOpenGL failed");
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init(IMGUI_GLSL_VERSION)) {
        logError("AppWindow: ImGui_ImplOpenGL3_Init failed");
        return false;
    }
    return true;
}

void AppWindow::shutdownImGui() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void AppWindow::mainLoop() {
    while (!glfwWindowShouldClose(asWindow(handle_))) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (onDraw_) onDraw_();
        if (onUI_)   onUI_();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(asWindow(handle_));
    }

    shutdownImGui();
    glfwDestroyWindow(asWindow(handle_));
    handle_ = nullptr;
    glfwTerminate();
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
    glViewport(0, 0, w, h);
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
