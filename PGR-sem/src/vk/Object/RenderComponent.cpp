module;
#include <glm/glm.hpp>

module RenderComponent;

void RenderComponent::onAddedToScene() {
    Scene* scene = getOwner().getScene();
    if (!scene || !multiMesh_) return;

    /* Assigning drops any previous entry through the moved-from handle. */
    handle_ = scene->getDrawList().add(
        DrawItem{multiMesh_, getWorldTransform(), isVisible_});
}

void RenderComponent::onWorldTransformChanged() {
    handle_.setTransform(getWorldTransform());
}
