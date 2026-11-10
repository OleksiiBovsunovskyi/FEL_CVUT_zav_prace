module;
#include <glm/glm.hpp>

module VkScene;

Transform Component::getWorldTransform() const {
    return getOwner().getTransform() * getTransform();
}
