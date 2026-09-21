module;
#include <filesystem>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
export module ufo;

import QuadBeizer;
import boundingBox;
import Logger.gl;
import Scene;
import StaticMesh;

export class Ufo
{
public:
    Ufo(std::filesystem::path assetPath, Scene* scene, const BoundingBox& bounds)
        : scene(scene)
    {
        constexpr float HEIGHT = 13.0f;
        const glm::vec3 mn = bounds.min;
        const glm::vec3 mx = bounds.max;

        const glm::vec3 A = glm::vec3(mx.x, HEIGHT, mn.z);
        const glm::vec3 B = glm::vec3(mx.x, HEIGHT, mx.z);
        const glm::vec3 C = glm::vec3(mn.x, HEIGHT, mx.z);
        const glm::vec3 D = glm::vec3(mn.x, HEIGHT, mn.z);

        curve0 = new QuadBeizerCurve(A, B, C, SEGMENT_DURATION);
        curve1 = new QuadBeizerCurve(C, D, A, SEGMENT_DURATION);

        mesh = scene->addMesh(assetPath);
        if (!mesh)
        {
            logError("Failed to create ufo mesh");
            return;
        }
        mesh->setScale(glm::vec3(0.01));
        applyTransform(0.0f);

    }

    ~Ufo()
    {
        delete curve0;
        delete curve1;
    }

    glm::vec3 getPosition() const
    {
        return mesh ? mesh->getPosition() : glm::vec3(0.0f);
    }

    void update(float dt)
    {
        segmentTime += dt;
        if (segmentTime >= SEGMENT_DURATION)
        {
            segmentTime -= SEGMENT_DURATION;
            currentCurve = 1 - currentCurve;
        }
        spinAngle += SPIN_SPEED * dt;
        applyTransform(segmentTime);
    }

private:
    static constexpr float SEGMENT_DURATION = 2.5f;
    static constexpr float TANGENT_EPS      = 0.01f;
    static constexpr float SPIN_SPEED       = 222.0f; 

    void applyTransform(float t)
    {
        if (!mesh || !curve0 || !curve1) return;

        const QuadBeizerCurve& curve = (currentCurve == 0) ? *curve0 : *curve1;

        glm::vec3 pos = curve.getPathPoint(t);

        float t1 = std::min(t + TANGENT_EPS, SEGMENT_DURATION);
        float t0 = std::max(t - TANGENT_EPS, 0.0f);
        glm::vec3 dir = glm::normalize(curve.getPathPoint(t1) - curve.getPathPoint(t0));

        static constexpr float MODEL_FORWARD_OFFSET = -1.5707963f;
        const float yaw = std::atan2f(dir.x, dir.z) + MODEL_FORWARD_OFFSET;
        const glm::quat qYaw  = glm::angleAxis(yaw,       glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::quat qSpin = glm::angleAxis(spinAngle, glm::vec3(0.0f, 1.0f, 0.0f));
    
        mesh->setRotation(qYaw * qSpin);
        mesh->setPosition(pos);

    }

    Scene* scene        = nullptr;
    StaticMesh* mesh         = nullptr;
    QuadBeizerCurve* curve0      = nullptr;
    QuadBeizerCurve* curve1      = nullptr;
    int currentCurve = 0;
    float segmentTime  = 0.0f;
    float spinAngle    = 0.0f;
};
