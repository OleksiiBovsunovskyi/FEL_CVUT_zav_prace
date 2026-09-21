module;
#include <filesystem>
#include <random>
#include <glm/glm.hpp>
export module helicopter;

import boundingBox;
import lightsource.spotlight;
import Logger.gl;
import Scene;
import StaticMesh;

export class Helicopter
{
public:
    Helicopter(std::filesystem::path assetPath, Scene* scene, BoundingBox sceneBounds, float height)
        : scene(scene), sceneBounds(sceneBounds), height(height)
    {
        mesh = scene->addMesh(assetPath);
        if (!mesh)
        {
            logError("Failed to create helicopter mesh");
            return;
        }
        
        std::mt19937 rng(std::random_device{}());
        auto randRange = [&](float lo, float hi) {
            return std::uniform_real_distribution<float>(lo, hi)(rng);
        };

        position = glm::vec3(
            randRange(sceneBounds.min.x, sceneBounds.max.x),
            height,
            randRange(sceneBounds.min.z, sceneBounds.max.z)
        );

        float angle = randRange(0.0f, 6.2831853f);
        velocity = glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * speed;

        mesh->setPosition(position);
        mesh->setScale(glm::vec3(10));
        
        const BoundingBox bounds = mesh->getBounds();
        glm::vec3 lightPos = glm::vec3(position.x, bounds.min.y, position.z);

        SpotLight lightInit;
        lightInit.setColor(glm::vec3(10.0f, 10.85f, 10.6f));
        lightInit.setAttenuation(0,0,0.01);
        lightInit.setCastsShadow(true);
        lightInit.setShadowFar(100.0f);
        lightInit.setPosition(lightPos);
        lightInit.setDirection(glm::vec3(0.0f, -1.0f, 0.0f));

        helicopterLight = &scene->addSpotLight(lightInit);
    }

    void update(float dt)
    {
        position += velocity * dt;
        
        if (position.x < sceneBounds.min.x || position.x > sceneBounds.max.x)
        {
            velocity.x = -velocity.x;
            position.x = glm::clamp(position.x, sceneBounds.min.x, sceneBounds.max.x);
        }
        if (position.z < sceneBounds.min.z || position.z > sceneBounds.max.z)
        {
            velocity.z = -velocity.z;
            position.z = glm::clamp(position.z, sceneBounds.min.z, sceneBounds.max.z);
        }

        position.y = height; 

        if (mesh)
            mesh->setPosition(position);

        if (helicopterLight && mesh)
        {
            const BoundingBox bounds = mesh->getBounds();
            glm::vec3 lightPos = glm::vec3(position.x, bounds.min.y, position.z);
            helicopterLight->setPosition(lightPos);
            helicopterLight->setDirection(glm::vec3(0.0f, -1.0f, 0.0f));
        }
    }

private:
    Scene*       scene           = nullptr;
    StaticMesh*  mesh            = nullptr;
    BoundingBox  sceneBounds;
    SpotLight*   helicopterLight = nullptr;

    glm::vec3    position{0.0f};
    glm::vec3    velocity{0.0f};
    float        height  = 20.0f;
    float        speed   = 1.0f;
};
