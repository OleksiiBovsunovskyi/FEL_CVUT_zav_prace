module;
#include <utility>

#include <glm/glm.hpp>

module RenderComponent;

void RenderComponent::registerParts() {
    Scene* scene = getOwner().getScene();
    if (!scene || !multiMesh_) return;

    const glm::mat4 world = getWorldTransform();
    DrawList&       drawList = scene->getDrawList();

    parts_.reserve(multiMesh_->size());
    for (const MultiMeshPart& part : multiMesh_->parts()) {
        const Mesh& mesh = *part.mesh;
        if (!mesh.uploaded() || mesh.meshletCount() == 0) continue;

        GPUMeshInstance instance{};
        instance.transform = world * part.localTransform;
        instance.mesh      = mesh.header();

        const MeshBounds& bounds = mesh.bounds();
        parts_.push_back(RegisteredPart{
            drawList.add(instance, glm::vec4(bounds.center, bounds.radius)),
            part.localTransform});
    }
}

void RenderComponent::setVisible(bool visible) {
    if (visible == isVisible_) return;
    isVisible_ = visible;

    if (visible) registerParts();
    else parts_.clear();
}

void RenderComponent::onAddedToScene() {
    if (isVisible_) registerParts();
}

void RenderComponent::onWorldTransformChanged() {
    const glm::mat4 world = getWorldTransform();
    for (RegisteredPart& part : parts_)
        part.handle.setTransform(world * part.localTransform);
}
