module;
#include <vulkan/vulkan_core.h>

#include <functional>

export module VkWindow;

import vulkan;
/**
 * Window + input + ImGui host, backed by GLFW. Vulkan port of Window/AppWindow.ixx.
 */
export class AppWindow {
public:
    AppWindow() = default;
    ~AppWindow() = default;

    bool init(int width, int height, const char* title);

    /**
     * Polls, builds the ImGui frame and drives the draw callback until the
     * window is closed. Does not destroy anything; call shutdown() afterward.
     */
    void mainLoop();

    /**
     * Destroys the ImGui context, the platform backend and the window.
     * Must run after the app has destroyed its Vulkan objects.
     */
    void shutdown();

    void setDrawCallback(std::function<void()> cb)                  { onDraw_        = std::move(cb); }
    void setResizeCallback(std::function<void(int, int)> cb)        { onResize_      = std::move(cb); }
    void setMouseMoveCallback(std::function<void(float, float)> cb) { onMouseMove_   = std::move(cb); }
    void setMouseButtonCallback(std::function<void(int, int)> cb)   { onMouseButton_ = std::move(cb); }
    void setKeyDownCallback(std::function<void(unsigned char)> cb)  { onKeyDown_     = std::move(cb); }

    /// Runs before the frame is recorded, inside the ImGui frame.
    void setUICallback(std::function<void()> cb)                    { onUI_          = std::move(cb); }

    // --- Vulkan interop -----------------------------------------------------

    /// Creates a VkSurfaceKHR for this window. The caller owns the surface.
    bool createSurface(VkInstance instance, VkSurfaceKHR* outSurface) const;

    void getFramebufferSize(int& width, int& height) const;

    /// Ends mainLoop() after the current frame, as if the window were closed.
    void requestClose();

    /**
     * Blocks on events while the window is minimised (zero-sized framebuffer),
     * so swapchain recreation never sees a 0x0 extent.
     */
    void waitWhileMinimized() const;

    // --- Input / state ------------------------------------------------------

    /// ASCII, case-insensitive
    bool isKeyDown(unsigned char key) const;
    bool isUIMode()                   const { return uiMode_; }
    void setUIMode(bool enable);

    int getWidth()   const { return width_; }
    int getHeight()  const { return height_; }
    int getCenterX() const { return centerX_; }
    int getCenterY() const { return centerY_; }

    /// Milliseconds since GLFW init;
    int getElapsedMs() const;

    static AppWindow* current() { return instance_; }
    void onFramebufferSize(int w, int h);
    void onKey(int key, int action);
    void onCursorPos(double x, double y);
    void onMouseButton(int button, int action);

private:
    void* handle_ = nullptr;   // GLFWwindow*

    int  width_ = 0, height_ = 0;
    int  centerX_ = 0, centerY_ = 0;
    bool uiMode_ = false;
    bool firstMouse_ = true;
    double lastMouseX_ = 0.0, lastMouseY_ = 0.0;
    bool keys_[256] = {};

    std::function<void()>              onDraw_;
    std::function<void(int, int)>      onResize_;
    std::function<void(float, float)>  onMouseMove_;
    std::function<void(int, int)>      onMouseButton_;
    std::function<void(unsigned char)> onKeyDown_;
    std::function<void()>              onUI_;

    static AppWindow* instance_;

    bool initImGui();
    void applyCursorMode();
};
