#include <cmath>
#include "pgr.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <cstdio>

#include "imgui.h"

import Camera;
import QuadBeizer;
import Scene;
import StaticMesh;
import Model;
import LightSource;
import postprocess.manager;
import Logger;
import Window;
import helicopter;
import ufo;
import boundingBox;
import SunConfigParser;

#ifdef __linux__
#include <GL/glx.h>
#endif

#include <assimp/version.h>

constexpr int WIN_WIDTH = 512;
constexpr int WIN_HEIGHT = 512;
constexpr char WIN_TITLE[] = "PGR_SEM";

constexpr float MOVE_SPEED = 20.0f;
constexpr float MOUSE_SENSITIVITY = 0.0005f;
constexpr float PITCH_MAX = 1.5607f;
constexpr float PITCH_MIN = -1.5607f;

constexpr const char* TRANSFORMS_FILE = "Assets/saved_camera_tranforms.txt";
constexpr const char* SPLINE_FILE = "Assets/spline_points.txt";
constexpr float SPLINE_DURATION = 4.0f;

AppWindow window;
Camera camera;
Scene scene;
std::shared_ptr<Helicopter> helicopter;
std::shared_ptr<Ufo> ufo;
StaticMesh* City = nullptr;
DirectionalLight* sun = nullptr;
PostProcessManager ppManager;

float yaw = 0.0f;
float pitch = 0.0f;
int prevTime = 0;
bool ufoFollow = false;

glm::vec3 calculateCameraDirection(float yaw, float pitch)
{
    return glm::vec3(
        std::cosf(pitch) * std::sinf(yaw),
        std::sinf(pitch),
        -std::cosf(pitch) * std::cosf(yaw)
    );
}

const glm::vec3 WORLD_UP(0.0f, 1.0f, 0.0f);

struct SavedCamera
{
    int idx;
    float yaw, pitch;
    glm::vec3 pos;
};

std::vector<SavedCamera> savedCameras;

void loadCameraTransforms()
{
    savedCameras.clear();
    std::ifstream f(TRANSFORMS_FILE);
    SavedCamera sc;
    while (f >> sc.idx >> sc.yaw >> sc.pitch >> sc.pos.x >> sc.pos.y >> sc.pos.z)
        savedCameras.push_back(sc);
}

void startSplineAnimation();

/// Jump the camera to a saved transform (was the GLUT right-click submenu).
void applySavedCamera(int index)
{
    if (index < 0 || index >= static_cast<int>(savedCameras.size())) return;
    const auto& sc = savedCameras[index];
    yaw = sc.yaw;
    pitch = sc.pitch;
    camera.setPosition(sc.pos);
    camera.setTarget(sc.pos + glm::normalize(calculateCameraDirection(yaw, pitch)));
}

void saveCameraTransform()
{
    loadCameraTransforms();
    int nextIdx = savedCameras.empty() ? 0 : savedCameras.back().idx + 1;
    std::ofstream f(TRANSFORMS_FILE, std::ios::app);
    const glm::vec3 pos = camera.getPosition();
    f << nextIdx << " " << yaw << " " << pitch << " "
        << pos.x << " " << pos.y << " " << pos.z << "\n";
    loadCameraTransforms();
}


struct SplinePoint
{
    int idx;
    float yaw, pitch;
    glm::vec3 pos;
};

bool splineActive = false;
float splineTime = 0.0f;
QuadBeizerCurve* splineCurve = nullptr;
QuadBeizerCurve* splineRotCurve = nullptr;
std::vector<SplinePoint> splinePoints;

void loadSplinePoints()
{
    splinePoints.clear();
    std::ifstream f(SPLINE_FILE);
    SplinePoint sp;
    while (f >> sp.idx >> sp.yaw >> sp.pitch >> sp.pos.x >> sp.pos.y >> sp.pos.z)
        splinePoints.push_back(sp);
}

void startSplineAnimation()
{
    loadSplinePoints();
    if (splinePoints.size() < 3) return;

    delete splineCurve;
    delete splineRotCurve;
    splineCurve = new QuadBeizerCurve(
        splinePoints[0].pos, splinePoints[1].pos, splinePoints[2].pos, SPLINE_DURATION);
    splineRotCurve = new QuadBeizerCurve(
        glm::vec3(splinePoints[0].yaw, splinePoints[0].pitch, 0.0f),
        glm::vec3(splinePoints[1].yaw, splinePoints[1].pitch, 0.0f),
        glm::vec3(splinePoints[2].yaw, splinePoints[2].pitch, 0.0f),
        SPLINE_DURATION);

    splineTime = 0.0f;
    splineActive = true;
    prevTime = window.getElapsedMs();

    yaw = splinePoints[0].yaw;
    pitch = splinePoints[0].pitch;
    camera.setPosition(splinePoints[0].pos);
    camera.setTarget(splinePoints[0].pos + glm::normalize(calculateCameraDirection(yaw, pitch)));
}


void initCameraAngles()
{
    glm::vec3 dir = glm::normalize(camera.getTarget() - camera.getPosition());
    pitch = std::asinf(dir.y);
    yaw = std::atan2f(dir.x, -dir.z);
}

void updateCamera(const float dt)
{
    if (ufoFollow && ufo)
    {
        const glm::vec3 pos = camera.getPosition();
        const glm::vec3 toUfo = ufo->getPosition() - pos;
        const float dist = glm::length(toUfo);
        if (dist > 0.001f)
        {
            const glm::vec3 dir = toUfo / dist;
            pitch = std::asinf(glm::clamp(dir.y, -1.0f, 1.0f));
            yaw = std::atan2f(dir.x, -dir.z);
        }
        camera.setTarget(pos + glm::normalize(calculateCameraDirection(yaw, pitch)));
        return;
    }

    if (splineActive && splineCurve)
    {
        splineTime += dt;
        if (splineTime >= splineCurve->getDuration())
        {
            splineTime = splineCurve->getDuration();
            splineActive = false;
        }
        const glm::vec3 pos = splineCurve->getPathPoint(splineTime);
        const glm::vec3 rot = splineRotCurve->getPathPoint(splineTime);
        yaw = rot.x;
        pitch = rot.y;
        const glm::vec3 forward = glm::normalize(calculateCameraDirection(yaw, pitch));
        camera.setPosition(pos);
        camera.setTarget(pos + forward);
        return;
    }

    const glm::vec3 forward = glm::normalize(calculateCameraDirection(yaw, pitch));
    const glm::vec3 right = glm::normalize(glm::cross(forward, WORLD_UP));

    glm::vec3 pos = camera.getPosition();

    if (window.isKeyDown('w') || window.isKeyDown('W')) pos += forward * MOVE_SPEED * dt;
    if (window.isKeyDown('s') || window.isKeyDown('S')) pos -= forward * MOVE_SPEED * dt;
    if (window.isKeyDown('a') || window.isKeyDown('A')) pos -= right * MOVE_SPEED * dt;
    if (window.isKeyDown('d') || window.isKeyDown('D')) pos += right * MOVE_SPEED * dt;
    if (window.isKeyDown('e') || window.isKeyDown('E')) pos += WORLD_UP * MOVE_SPEED * dt;
    if (window.isKeyDown('q') || window.isKeyDown('Q')) pos -= WORLD_UP * MOVE_SPEED * dt;

    camera.setPosition(pos);
    camera.setTarget(pos + forward);
}




void init()
{
    glClearColor(0.2f, 0.1f, 0.3f, 1.0f);
    glEnable(GL_DEPTH_TEST);

    if (!scene.init(WIN_WIDTH, WIN_HEIGHT))
        pgr::dieWithError("Scene init failed");
    scene.loadSkybox("Assets/Skybox");
    scene.setAmbientLight(glm::vec3(0.0f));

    SunConfigParser sunParser("Config/SunConfig.config");
    auto sunConfigOpt = sunParser.parseConfig();
    SunConfig sunCfg = sunConfigOpt.value_or(SunConfig{});

    DirectionalLight sunInit;
    sunInit.setDirection(glm::normalize(sunCfg.direction));
    sunInit.setColor(sunCfg.color);
    sunInit.setShadowFar(150.0f);
    sunInit.setShadowNear(1.0f);
    sunInit.setShadowTarget(glm::vec3(0.0f));
    sunInit.setCastsShadow(true);

    sun = &scene.addDirectionalLight(sunInit);
    City = scene.addMesh("Assets/Scene/City_scene_pgr.glb", RenderMode::Deferred);
    BoundingBox sceneBounds = City->getBounds();
    helicopter = std::make_shared<Helicopter>(
        "Assets/Helicopter/Helicopter.glb", &scene, sceneBounds, 10);
    ufo = std::make_shared<Ufo>("Assets/UFO/ufo.glb", &scene, sceneBounds);
    if (!City)
        pgr::dieWithError("Failed to load model");
    City->setRotation(glm::vec3(0, 0, 0));
    City->setPosition(glm::vec3(0, -21, 0));

    camera.setPosition(glm::vec3(0, 1, 100));
    camera.setNearPlane(0.1f);
    camera.setFarPlane(200.0f);
    camera.setFov(1000.0f);

    ppManager.init(scene, camera);
}


void draw()
{
    const int currentTime = window.getElapsedMs();
    const float dt = (currentTime - prevTime) / 1000.0f;
    prevTime = currentTime;

    helicopter->update(dt);
    ufo->update(dt);

    ppManager.updatePerf(dt);

    if (splineActive || !window.isUIMode())
        updateCamera(dt);

    ppManager.updateMatrices(camera);

    scene.draw(camera);
}


/// ImGui panel. Built each frame between scene rendering and the buffer swap.
void drawUI()
{
    ImGui::SetNextWindowSize(ImVec2(320.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("PGR"))
    {
        ImGui::Text("Hello, world!");
        ImGui::Separator();

        ImGui::Text("%.1f FPS  (%.2f ms)", ppManager.displayFPS, ppManager.displayFrameMs);
        ImGui::Text("%d x %d", window.getWidth(), window.getHeight());
        ImGui::TextUnformatted(window.isUIMode() ? "UI mode  (Tab to fly)"
                                                 : "Fly mode (Tab for UI)");

        ImGui::Separator();
        if (ImGui::Button("Animate along curve"))
            startSplineAnimation();

        if (ImGui::CollapsingHeader("Post-process", ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (int i = 0; i < IM_ARRAYSIZE(PostProcessManager::SLOTS); ++i)
            {
                const int slot = PostProcessManager::SLOTS[i];
                if (ImGui::Checkbox(PostProcessManager::SLOT_NAMES[i],
                                    ppManager.effectFlag(slot)))
                    ppManager.setEffectEnabled(slot, ppManager.isEffectEnabled(slot));
            }

            if (ImGui::Button("All off"))
                ppManager.setAllEffectsEnabled(false);
            ImGui::SameLine();
            if (ImGui::Button("All on"))
                ppManager.setAllEffectsEnabled(true);
        }

        // Replaces the old right-click GLUT menu, which GLFW has no equivalent for.
        if (ImGui::CollapsingHeader("Saved cameras"))
        {
            if (savedCameras.empty())
                ImGui::TextDisabled("none saved (press C)");

            for (int i = 0; i < static_cast<int>(savedCameras.size()); ++i)
            {
                const auto& sc = savedCameras[i];
                ImGui::PushID(i);
                if (ImGui::Button("go"))
                    applySavedCamera(i);
                ImGui::SameLine();
                ImGui::Text("#%d  (%.1f, %.1f, %.1f)", sc.idx, sc.pos.x, sc.pos.y, sc.pos.z);
                ImGui::PopID();
            }
        }
    }
    ImGui::End();
}

int main(int argc, char** argv)
{
    if (!window.init(argc, argv, WIN_WIDTH, WIN_HEIGHT, WIN_TITLE))
        pgr::dieWithError("pgr init failed");

    window.setDrawCallback(draw);
    window.setUICallback(drawUI);
    window.setResizeCallback([](int w, int h)
    {
        camera.setAspect(static_cast<float>(w) / static_cast<float>(h));
        scene.resize(w, h);
        ppManager.resize(w, h);
    });
    window.setMouseMoveCallback([](float dx, float dy)
    {
        if (ufoFollow) return;
        yaw += dx * MOUSE_SENSITIVITY;
        pitch -= dy * MOUSE_SENSITIVITY;
        pitch = glm::clamp(pitch, PITCH_MIN, PITCH_MAX);
    });
    window.setKeyDownCallback([](unsigned char key)
    {
        if (key == 'u' || key == 'U') ufoFollow = !ufoFollow;
        if (key == 'c' || key == 'C') saveCameraTransform();
        if (key == 'z' || key == 'Z') scene.addSphere(camera.getPosition());
        if (key == 'p' || key == 'P') {
            PointLight light;
            light.setPosition(camera.getPosition());
            light.setColor(glm::vec3(1.0f, 1.0f, 1.0f));
            scene.addPointLight(light);
        }
    });
    
    init();
    loadCameraTransforms();

    initCameraAngles();
    prevTime = window.getElapsedMs();

    window.mainLoop();
    return 0;
}
