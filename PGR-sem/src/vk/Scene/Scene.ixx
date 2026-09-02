module;
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

export module VkScene;

export import :Transformable;
export import :Component;
export import :Object;
export import :CameraComponent;
export import DrawList;

/**
 * Everything placed in the world, the list of what wants drawing, and which
 * camera is looking.
 *
 * Owns the Objects; owns the DrawList only because it is the one place every
 * Object can reach. 
 */
export class Scene {
public:
    /**
     * Takes ownership of a built Object and attaches its components.
     *
     * @return a reference that stays valid until the Object is removed. 
     */
    Object& addObject(std::unique_ptr<Object> object);

    void clearObjects();

    [[nodiscard]] std::size_t getObjectsCount() const { return objects_.size(); }
    [[nodiscard]] bool isEmpty() const { return objects_.empty(); }

    /// What the components registered themselves in.
    [[nodiscard]] DrawList& getDrawList() { return drawList_; }
    [[nodiscard]] const DrawList& getDrawList() const { return drawList_; }

    
    [[nodiscard]] CameraComponent* getActiveCamera() const { return activeCamera_; }
    void setActiveCamera(CameraComponent* camera) { activeCamera_ = camera; }

private:
    /* Declared first, so it is destroyed last: the components unregister from
     * it while their Objects die, and a DrawList that went first would be
     * written to after its lifetime ended. */
    DrawList drawList_;
    
    
    std::vector<std::unique_ptr<Object>> objects_;
    CameraComponent* activeCamera_ = nullptr;
};
