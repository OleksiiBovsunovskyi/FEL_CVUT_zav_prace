module;
#include "pgr.h"
#include <functional>

export module Window;

import Logger.gl;

/**
 * Window + input + ImGui host, backed by GLFW.
 * !TODO: rewrite
 */
export class AppWindow {
public:
    AppWindow() = default;
    ~AppWindow() = default;

    bool init(int argc, char** argv, int width, int height, const char* title);
    void mainLoop();

    void setDrawCallback(std::function<void()> cb)                  { onDraw_        = std::move(cb); }
    void setResizeCallback(std::function<void(int, int)> cb)        { onResize_      = std::move(cb); }
    void setMouseMoveCallback(std::function<void(float, float)> cb) { onMouseMove_   = std::move(cb); }
    void setMouseButtonCallback(std::function<void(int, int)> cb)   { onMouseButton_ = std::move(cb); }
    void setKeyDownCallback(std::function<void(unsigned char)> cb)  { onKeyDown_     = std::move(cb); }

    /// Runs after the scene is drawn and before the buffer swap;
    void setUICallback(std::function<void()> cb)                    { onUI_          = std::move(cb); }

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
    void shutdownImGui();
    void applyCursorMode();
};
