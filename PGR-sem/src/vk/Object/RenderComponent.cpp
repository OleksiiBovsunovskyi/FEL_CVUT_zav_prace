module;
#include <glm/glm.hpp>

module RenderComponent;

void RenderComponent::onAddedToScene() {
    Scene* scene = getOwner().getScene();
    if (!scene || !multiMesh_) return;

    /* Assigning drops any previous entry through the moved-from handle. */
    handle_ = scene->getDrawList().add(
        DrawItem{multiMesh_, getOwner().getTransform(), isVisible_});
}

void RenderComponent::onOwnerTransformChanged() {
    handle_.setTransform(getOwner().getTransform());
}
