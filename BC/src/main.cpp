#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <utility>
#include <array>
#include <format>
#include <random>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>


import Logger;
import VulkanApp;
import VkScene;
import RenderComponent;
import MultiMesh;
import Player;

int main(int argc, char** argv) {
    try {
        /* argv: <model.pmma> [frame budget]. The build bakes one .pmma per
         * model in Assets and copies them next to the executable. */
        const std::filesystem::path modelPath =
            argc > 1 ? std::filesystem::path{argv[1]}
                     : std::filesystem::path{"Assets"} / "City_scene_pgr.pmma";
        
        VulkanApp app{argc > 2 ? static_cast<uint32_t>(std::atoi(argv[2])) : 0u};
        app.init();

        /* What the world contains is decided here; the app only renders it. */
        std::shared_ptr<MultiMesh> model = app.getLoader().loadModel(modelPath);
        if (!model) {
            std::fprintf(stderr, "fatal: failed to load %s\n",
                         modelPath.string().c_str());
            return EXIT_FAILURE;
        }
        //Test 1 mil objects
       //TODO: 18ms on 1000 car models 140ms on 10k, 1000 on 100k, silent crash on 1m (on 100k logs: mesh-draw buffer capacity exceeded)
        //Res has small effect on performance. as well as release vs debug build. Bottelnek likelly on gpu
        //Cpu at ~5% load, while gpu on ~80. Maybe long idling due to sync
        //1M objects crash resolved, was stackoverflow due to use of Array to store objects.
        constexpr uint64_t ObjCount = 1;
        std::vector<Object*> objects;
        objects.reserve(ObjCount);

        std::mt19937 rng{std::random_device{}()};
        std::uniform_real_distribution<float> pos{-5000.0f, 5000.0f};

        for (uint64_t i = 0; i < ObjCount; ++i) {
            auto object = std::make_unique<Object>();

            object->addComponent(RenderComponent{model});

            const glm::vec3 position{
                pos(rng),
                pos(rng),
                pos(rng)
            };

            const glm::mat4 transform =
                glm::translate(glm::mat4{1.0f}, position);

            //object->setTransform(transform);

            objects.push_back(object.get());
            app.getScene().addObject(std::move(object));
        }
        /* Holds the camera, so this is also what decides where the view
         * starts; VulkanApp sizes its speed to the scene. */
        app.getScene().addObject(std::make_unique<Player>());

        uint64_t vertsPerObject = 0;
        for (const MultiMeshPart& part : model->parts())
            vertsPerObject += part.mesh->vertexCount();

        logMessage(std::format(
            "scene: {} objects x {} meshes = {} meshes, {} vertices",
            formatCount(ObjCount), model->size(),
            formatCount(ObjCount * model->size()),
            formatCount(ObjCount * vertsPerObject)));

        app.run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fatal: %s\n", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
