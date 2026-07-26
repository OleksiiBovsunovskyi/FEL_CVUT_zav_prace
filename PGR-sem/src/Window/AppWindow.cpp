module;
#include "pgr.h"
#include "AntTweakBar.h"

module Window;

AppWindow* AppWindow::instance_ = nullptr;

bool AppWindow::init(int argc, char** argv, int width, int height, const char* title) {
    instance_ = this;
    width_   = width;
    height_  = height;
    centerX_ = width  / 2;
    centerY_ = height / 2;

    glutInit(&argc, argv);
    glutInitContextVersion(pgr::OGL_VER_MAJOR, pgr::OGL_VER_MINOR);
    glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_DEPTH);
    glutInitWindowSize(width, height);
    glutCreateWindow(title);

    glutDisplayFunc(glutDrawCB);
    glutReshapeFunc(glutReshapeCB);
    glutIdleFunc(glutDrawCB);

    glutKeyboardFunc(glutKeyDownCB);
    glutKeyboardUpFunc(glutKeyUpCB);
    glutMouseFunc(glutMouseButtonCB);
    glutMotionFunc(glutMouseDragCB);
    glutPassiveMotionFunc(glutMousePassiveCB);

    glutSpecialFunc((GLUTspecialfun)TwEventSpecialGLUT);

    TwGLUTModifiersFunc(glutGetModifiers);

    glutSetCursor(GLUT_CURSOR_NONE);

    return pgr::initialize(pgr::OGL_VER_MAJOR, pgr::OGL_VER_MINOR);
}

void AppWindow::mainLoop() {
    glutMainLoop();
}

void AppWindow::setUIMode(bool enable) {
    uiMode_ = enable;
    if (uiMode_) {
        glutSetCursor(GLUT_CURSOR_LEFT_ARROW);
    } else {
        glutSetCursor(GLUT_CURSOR_NONE);
        mouseWarping_ = true;
        glutWarpPointer(centerX_, centerY_);
    }
}

void AppWindow::handleMouseMotion(int x, int y) {
    if (mouseWarping_) { mouseWarping_ = false; return; }
    if (onMouseMove_)
        onMouseMove_(static_cast<float>(x - centerX_), static_cast<float>(y - centerY_));
    mouseWarping_ = true;
    glutWarpPointer(centerX_, centerY_);
}

void AppWindow::glutDrawCB() {
    if (instance_->onDraw_) instance_->onDraw_();
}

void AppWindow::glutReshapeCB(int w, int h) {
    glViewport(0, 0, w, h);
    instance_->width_   = w;
    instance_->height_  = h;
    instance_->centerX_ = w / 2;
    instance_->centerY_ = h / 2;
    TwWindowSize(w, h);
    if (instance_->onResize_) instance_->onResize_(w, h);
}

void AppWindow::glutKeyDownCB(unsigned char key, int x, int y) {
    if (key == 27) exit(0);
    if (key == '\t') { instance_->setUIMode(!instance_->uiMode_); return; }
    if (instance_->uiMode_ && TwEventKeyboardGLUT(key, x, y)) return;
    instance_->keys_[key] = true;
    if (instance_->onKeyDown_) instance_->onKeyDown_(key);
}

void AppWindow::glutKeyUpCB(unsigned char key, int x, int y) {
    if (instance_->uiMode_) return;
    instance_->keys_[key] = false;
}

void AppWindow::glutMouseButtonCB(int button, int state, int x, int y) {
    if (instance_->uiMode_) { TwEventMouseButtonGLUT(button, state, x, y); return; }
    if (instance_->onMouseButton_) instance_->onMouseButton_(button, state);
}

void AppWindow::glutMouseDragCB(int x, int y) {
    if (instance_->uiMode_) TwEventMouseMotionGLUT(x, y);
}

void AppWindow::glutMousePassiveCB(int x, int y) {
    if (instance_->uiMode_) { TwEventMouseMotionGLUT(x, y); return; }
    instance_->handleMouseMotion(x, y);
}
