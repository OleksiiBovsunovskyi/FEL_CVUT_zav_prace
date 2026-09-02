module;
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

export module VkScene;

export import :Transformable;
export import :Subscribers;
export import :EventCapable;
export import :Event;
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
     * @return a reference of the type handed in
     */
    template <typename T>
    T& addObject(std::unique_ptr<T> object) {
        T& stored = *object;
        objects_.push_back(std::move(object));
        subscriptions_.subscribe(stored);
        stored.onAddedToScene(*this);
        return stored;
    }

    void clearObjects();

    /**
     * Broadcasts a tick event to everyone that subscribed 
     * @param deltaSeconds time since the last tick event, in seconds.
     */
    void tick(float deltaSeconds);

    [[nodiscard]] std::size_t getObjectsCount() const { return objects_.size(); }
    [[nodiscard]] bool isEmpty() const { return objects_.empty(); }

    /// What the components registered themselves in.
    [[nodiscard]] DrawList& getDrawList() { return drawList_; }
    [[nodiscard]] const DrawList& getDrawList() const { return drawList_; }

    
    [[nodiscard]] CameraComponent* getActiveCamera() const { return activeCamera_; }
    void setActiveCamera(CameraComponent* camera) { activeCamera_ = camera; }

private:
    /// Objects hand their pending subscriptions over on joining.
    friend class Object;

    /* Declared first, so it is destroyed last: the components unregister from
     * it while their Objects die, and a DrawList that went first would be
     * written to after its lifetime ended. */
    DrawList drawList_;


    std::vector<std::unique_ptr<Object>> objects_;
    CameraComponent* activeCamera_ = nullptr;
 
    AllEventSubscriptions subscriptions_;
};
