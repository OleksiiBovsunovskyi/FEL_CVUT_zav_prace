module;
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

export module MultiMesh;

export import VK_Mesh;
export import Transform;

export struct MultiMeshPart {
    std::shared_ptr<VK_Mesh> mesh;
    Transform localTransform{};
};

/// Owns group of meshes, allowing for logical multimaterial objects
export class MultiMesh {
public:
    void add(std::shared_ptr<VK_Mesh> mesh,
             const Transform& localTransform = Transform{}) {
        if (mesh)
            parts_.push_back({std::move(mesh), localTransform});
    }

    [[nodiscard]] std::span<const MultiMeshPart> parts() const {
        return parts_;
    }
    [[nodiscard]] size_t size() const { return parts_.size(); }
    [[nodiscard]] bool empty() const { return parts_.empty(); }

private:
    std::vector<MultiMeshPart> parts_;
};
