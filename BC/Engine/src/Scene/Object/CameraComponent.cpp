module;
#include <glm/glm.hpp>

module VkScene;

CameraComponent::~CameraComponent() {
    if (registeredScene_ && registeredScene_->getActiveCamera() == this)
        registeredScene_->setActiveCamera(nullptr);
}

void CameraComponent::onAddedToScene() {
    Scene* scene = getOwner().getScene();
    if (!scene || scene->getActiveCamera()) return;

    scene->setActiveCamera(this);
    registeredScene_ = scene;
}
