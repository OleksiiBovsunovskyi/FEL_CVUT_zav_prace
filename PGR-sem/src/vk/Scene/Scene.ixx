module;
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

export module VkScene;

export import :Component;
export import :Object;
export import DrawList;

/**
 * Everything placed in the world, and the list of what wants drawing.
 *
 * Owns the Objects; owns the DrawList only because it is the one place every
 * Object can reach. It never reads the list, never renders, knows no Vulkan
 * object, and names no component type: a component that needs the list takes
 * it from here when its Object is added.
 */
export class Scene {
public:
    /**
     * Takes ownership of a built Object and attaches its components.
     *
     * @return a reference that stays valid until the Object is removed.
     */
    Object& addObject(Object object);

    void clearObjects();

    [[nodiscard]] std::size_t getObjectsCount() const { return objects_.size(); }
    [[nodiscard]] bool isEmpty() const { return objects_.empty(); }

    /// Read-only: addObject is the only way in.
    [[nodiscard]] std::span<const Object> getObjects() const { return objects_; }

    /// What the components registered themselves in.
    [[nodiscard]] DrawList& getDrawList() { return drawList_; }
    [[nodiscard]] const DrawList& getDrawList() const { return drawList_; }

private:
    /* Declared first, so it is destroyed last: the components unregister from
     * it while their Objects die, and a DrawList that went first would be
     * written to after its lifetime ended. */
    DrawList drawList_;

    /* Objects move on growth; Object's move operations repoint the components
     * that point back at it. */
    std::vector<Object> objects_;
};
