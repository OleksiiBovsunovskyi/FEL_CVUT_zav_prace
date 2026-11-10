module;
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

export module Transform;

/**
 * Placement inside its parent space.
 */
export struct Transform {
    glm::vec3 position{0.0f};
    glm::vec3 scale{1.0f};
    /// Unit quaternion.
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};

    /// @return local-to-parent matrix: translation * rotation * scale.
    [[nodiscard]] glm::mat4 toMatrix() const {
        return glm::translate(glm::mat4{1.0f}, position) * glm::mat4_cast(rotation) *
               glm::scale(glm::mat4{1.0f}, scale);
    }

    /**
     * @param parent placement of the parent space in its own parent
     * @param child placement inside the parent space
     * @return placement of child in the parent's parent.
     */
    [[nodiscard]] friend Transform operator*(const Transform& parent, const Transform& child) {
        return Transform{parent.position + parent.rotation * (parent.scale * child.position),
                         parent.scale * child.scale,
                         parent.rotation * child.rotation};
    }
};

static_assert(sizeof(Transform) == 40);
