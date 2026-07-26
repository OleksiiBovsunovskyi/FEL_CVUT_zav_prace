module;
#include "pgr.h"
#include "AntTweakBar.h"
#include <functional>

export module Window;

import Logger;

export class AppWindow {
public:
    AppWindow() = default;
    ~AppWindow() = default;

    bool init(int argc, char** argv, int width, int height, const char* title);
    void mainLoop();

    void setDrawCallback(std::function<void()> cb)                { onDraw_        = std::move(cb); }
    void setResizeCallback(std::function<void(int, int)> cb)      { onResize_      = std::move(cb); }
    void setMouseMoveCallback(std::function<void(float, float)> cb) { onMouseMove_ = std::move(cb); }
    void setMouseButtonCallback(std::function<void(int, int)> cb) { onMouseButton_ = std::move(cb); }
    void setKeyDownCallback(std::function<void(unsigned char)> cb) { onKeyDown_    = std::move(cb); }

    bool isKeyDown(unsigned char key) const { return keys_[key]; }
    bool isUIMode()                   const { return uiMode_; }
    void setUIMode(bool enable);

    int getWidth()   const { return width_; }
    int getHeight()  const { return height_; }
    int getCenterX() const { return centerX_; }
    int getCenterY() const { return centerY_; }

private:
    int  width_ = 0, height_ = 0;
    int  centerX_ = 0, centerY_ = 0;
    bool mouseWarping_ = false;
    bool uiMode_ = false;
    bool keys_[256] = {};

    std::function<void()>             onDraw_;
    std::function<void(int, int)>     onResize_;
    std::function<void(float, float)> onMouseMove_;
    std::function<void(int, int)>     onMouseButton_;
    std::function<void(unsigned char)> onKeyDown_;

    static AppWindow* instance_;

    void handleMouseMotion(int x, int y);

    static void glutDrawCB();
    static void glutReshapeCB(int w, int h);
    static void glutKeyDownCB(unsigned char key, int x, int y);
    static void glutKeyUpCB(unsigned char key, int x, int y);
    static void glutMouseButtonCB(int button, int state, int x, int y);
    static void glutMouseDragCB(int x, int y);
    static void glutMousePassiveCB(int x, int y);
};
