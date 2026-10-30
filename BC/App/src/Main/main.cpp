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
  
    
        VulkanApp app{0};
        app.init();

        /* What the world contains is decided here; the app only renders it. */
    
        const std::filesystem::path modelPath = std::filesystem::path{"Assets"} / "san-miguel.pmma";
        std::shared_ptr<MultiMesh> model = app.getLoader().loadModel(modelPath);
    
        if (!model) {
            std::fprintf(stderr, "fatal: failed to load %s\n",
                         modelPath.string().c_str());
            return EXIT_FAILURE;
        }
        
        // Create scene object
        std::unique_ptr<Object> scene_obj = std::make_unique<Object>();
        scene_obj->addComponent(RenderComponent(model));
        Object& scene = app.getScene().addObject(std::move(scene_obj));
    
        //Put player in the world
        Player& player = app.getScene().addObject(std::make_unique<Player>());
        player.getMovement().setSpeed( 1);

        app.run();

    return EXIT_SUCCESS;
}
