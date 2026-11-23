#include <array>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <utility>

//Makes entry available in dll
#define APP_API extern "C" __declspec(dllexport)

import vulkan;
import PresentationBridge;
import VulkanApp;
import VkScene;
import RenderComponent;
import MultiMesh;
import Player;

/**
 * Starts the engine and scene on a dedicated native thread.
 * @param config startup paths, initial extent, optional GPU UUID, and host callbacks.
 * @return running app handle.
 */
APP_API AppHandle* app_start(const AppStartConfig* config) {
    if (!config || !config->dataRoot || config->width == 0 || config->height == 0) {
        PresentationBridge::setLastError("app_start: invalid configuration");
        return nullptr;
    }

    try {
        const std::filesystem::path root(reinterpret_cast<const char8_t*>(config->dataRoot));
        const vk::Extent2D initialExtent{config->width, config->height};

        std::array<uint8_t, 16> deviceUuid{};
        const bool hasUuid = config->deviceUuid != nullptr;
        if (hasUuid) std::memcpy(deviceUuid.data(), config->deviceUuid, deviceUuid.size());

        auto handle = std::make_unique<AppHandle>(config->callbacks);
        AppHandle* raw = handle.get();

        raw->worker = std::thread([raw, root, initialExtent, deviceUuid, hasUuid] {
            try {
                VulkanApp app;
                const std::span<const uint8_t> uuid =
                    hasUuid ? std::span<const uint8_t>{deviceUuid} : std::span<const uint8_t>{};
                app.init(raw->bridge, initialExtent, root, uuid);

                const std::filesystem::path modelPath = root / "Assets" / "FatMonkey8b.pmma";
                std::shared_ptr<MultiMesh> model = app.getLoader().loadModel(modelPath);
                if (!model) {
                    raw->bridge.notifyError("failed to load " + modelPath.string());
                    return;
                }

                auto sceneObject = std::make_unique<Object>();
                sceneObject->addComponent(RenderComponent(model));
                app.getScene().addObject(std::move(sceneObject));

                Player& player = app.getScene().addObject(std::make_unique<Player>());
                player.getMovement().setSpeed(1);

                app.run();
            } catch (const std::exception& e) {
                raw->bridge.notifyError(e.what());
            } catch (...) {
                raw->bridge.notifyError("unknown exception");
            }
        });

        return handle.release();
    } catch (const std::exception& e) {
        PresentationBridge::setLastError(e.what());
    } catch (...) {
        PresentationBridge::setLastError("unknown exception");
    }
    return nullptr;
}
