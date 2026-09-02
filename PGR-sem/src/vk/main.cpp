#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <utility>

import VulkanApp;
import VkScene;
import RenderComponent;
import MultiMesh;

int main(int argc, char** argv) {
    try {
        /* argv: <model> [frame budget] */
        const std::filesystem::path defaultModelPath =
            std::filesystem::path{__FILE__}.parent_path() / "Assets" / "scene.glb";
        const std::filesystem::path modelPath =
            argc > 1 ? std::filesystem::path{argv[1]} : defaultModelPath;

        VulkanApp app{argc > 2 ? static_cast<uint32_t>(std::atoi(argv[2])) : 0u};
        app.init();

        /* What the world contains is decided here; the app only renders it. */
        std::shared_ptr<MultiMesh> model = app.getLoader().loadModel(modelPath);
        if (!model) {
            std::fprintf(stderr, "fatal: failed to load %s\n",
                         modelPath.string().c_str());
            return EXIT_FAILURE;
        }

        Object object{};
        object.addComponent(RenderComponent{std::move(model)});
        app.getScene().addObject(std::move(object));

        app.run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fatal: %s\n", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
