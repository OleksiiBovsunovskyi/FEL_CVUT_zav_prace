module;
#include <glm/glm.hpp>

module VkScene;

glm::mat4 Component::getWorldTransform() const {
    return getOwner().getTransform() * getTransform();
}
