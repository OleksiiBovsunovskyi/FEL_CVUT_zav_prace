module;
#include <utility>

#include <glm/glm.hpp>

module RenderComponent;

void RenderComponent::registerParts() {
    Scene* scene = getOwner().getScene();
    if (!scene || !multiMesh_) return;

    const Transform world = getWorldTransform();
    DrawList&       drawList = scene->getDrawList();

    parts_.reserve(multiMesh_->size());
    for (const MultiMeshPart& part : multiMesh_->parts()) {
        const VK_Mesh& mesh = *part.mesh;
        if (!mesh.uploaded() || mesh.meshletCount() == 0) continue;

        const MeshBounds& bounds = mesh.bounds();
        parts_.push_back(RegisteredPart{
            drawList.add(mesh.header(), world * part.localTransform,
                         glm::vec4(bounds.center, bounds.radius)),
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
    const Transform world = getWorldTransform();
    for (RegisteredPart& part : parts_)
        part.handle.setTransform(world * part.localTransform);
}
